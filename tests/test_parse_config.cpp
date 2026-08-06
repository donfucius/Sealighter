#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <gtest/gtest.h>
#include "sealighter_handler.h"
#include "sealighter_errors.h"

#include <string>

// Helper: a minimal valid kernel_traces entry that can be appended to
// any config so that provider parsing succeeds after session_properties
// validation passes.
static const char* const kValidKernelTraces =
    R"("kernel_traces": [{)"
    R"("provider_name": "process",)"
    R"("trace_name": "test",)"
    R"("buffers": [])"
    R"(}])";

// Build a full config JSON from a session_properties body.
// The result always includes a valid kernel_traces section so that
// tests expecting ERROR_SUCCESS can reach provider parsing.
static std::string make_config(const std::string& session_props_body)
{
    return std::string("{") +
           R"("session_properties": {)" + session_props_body + "}," +
           kValidKernelTraces +
           "}";
}

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

class SealighterSession_ParseConfig_Test : public ::testing::Test {
protected:
    SealighterSession session;
};

// ===========================================================================
// 1. Invalid JSON
// ===========================================================================

TEST_F(SealighterSession_ParseConfig_Test, EmptyJson) {
    // {} has no user_traces or kernel_traces -> SEALIGHTER_ERROR_PARSE_NO_PROVIDERS
    int status = session.parse_config("{}");
    EXPECT_EQ(status, SEALIGHTER_ERROR_PARSE_NO_PROVIDERS);
}

TEST_F(SealighterSession_ParseConfig_Test, InvalidJson) {
    // Not valid JSON at all -> caught by nlohmann parse error handler -> error 4
    int status = session.parse_config("not json");
    EXPECT_EQ(status, SEALIGHTER_ERROR_PARSE_CONFIG_PROPS);
}

TEST_F(SealighterSession_ParseConfig_Test, NoProviders) {
    // session_properties present but no providers anywhere
    std::string config = R"({"session_properties": {}})";
    int status = session.parse_config(config);
    EXPECT_EQ(status, SEALIGHTER_ERROR_PARSE_NO_PROVIDERS);
}

// ===========================================================================
// 2. Buffer size validation  (must be 1..1024)
// ===========================================================================

TEST_F(SealighterSession_ParseConfig_Test, BufferSize_TooLow) {
    std::string config = make_config(R"("buffer_size": 0)");
    int status = session.parse_config(config);
    EXPECT_EQ(status, SEALIGHTER_ERROR_PARSE_CONFIG_PROPS);
}

TEST_F(SealighterSession_ParseConfig_Test, BufferSize_TooHigh) {
    std::string config = make_config(R"("buffer_size": 1025)");
    int status = session.parse_config(config);
    EXPECT_EQ(status, SEALIGHTER_ERROR_PARSE_CONFIG_PROPS);
}

// ===========================================================================
// 3. Minimum buffers validation  (must be 2..10000)
// ===========================================================================

TEST_F(SealighterSession_ParseConfig_Test, MinBuffers_TooLow) {
    std::string config = make_config(R"("minimum_buffers": 1)");
    int status = session.parse_config(config);
    EXPECT_EQ(status, SEALIGHTER_ERROR_PARSE_CONFIG_PROPS);
}

TEST_F(SealighterSession_ParseConfig_Test, MinBuffers_TooHigh) {
    std::string config = make_config(R"("minimum_buffers": 10001)");
    int status = session.parse_config(config);
    EXPECT_EQ(status, SEALIGHTER_ERROR_PARSE_CONFIG_PROPS);
}

// ===========================================================================
// 4. Maximum buffers validation  (must be >= minimum_buffers and <= 100000)
// ===========================================================================

TEST_F(SealighterSession_ParseConfig_Test, MaxBuffers_LessThanMin) {
    // minimum_buffers defaults to 12 when not explicitly set, but here we
    // set it to 10 explicitly; maximum_buffers: 5 < 10 -> error
    std::string config = make_config(
        R"("maximum_buffers": 5, "minimum_buffers": 10)");
    int status = session.parse_config(config);
    EXPECT_EQ(status, SEALIGHTER_ERROR_PARSE_CONFIG_PROPS);
}

TEST_F(SealighterSession_ParseConfig_Test, MaxBuffers_TooHigh) {
    std::string config = make_config(R"("maximum_buffers": 100001)");
    int status = session.parse_config(config);
    EXPECT_EQ(status, SEALIGHTER_ERROR_PARSE_CONFIG_PROPS);
}

// ===========================================================================
// 5. Flush timer validation  (must be 1..3600)
// ===========================================================================

TEST_F(SealighterSession_ParseConfig_Test, FlushTimer_TooLow) {
    std::string config = make_config(R"("flush_timer": 0)");
    int status = session.parse_config(config);
    EXPECT_EQ(status, SEALIGHTER_ERROR_PARSE_CONFIG_PROPS);
}

TEST_F(SealighterSession_ParseConfig_Test, FlushTimer_TooHigh) {
    std::string config = make_config(R"("flush_timer": 3601)");
    int status = session.parse_config(config);
    EXPECT_EQ(status, SEALIGHTER_ERROR_PARSE_CONFIG_PROPS);
}

// ===========================================================================
// 6. Output format
// ===========================================================================

TEST_F(SealighterSession_ParseConfig_Test, OutputFormat_Invalid) {
    std::string config = make_config(R"("output_format": "xml")");
    int status = session.parse_config(config);
    EXPECT_EQ(status, SEALIGHTER_ERROR_OUTPUT_FORMAT);
}

TEST_F(SealighterSession_ParseConfig_Test, OutputFormat_File_NoName) {
    // output_format "file" requires output_filename; without it -> error 13
    std::string config = make_config(R"("output_format": "file")");
    int status = session.parse_config(config);
    EXPECT_EQ(status, SEALIGHTER_ERROR_OUTPUT_FILE);
}

// ===========================================================================
// 7. Deprecated timeout (note the original typo: "timout")
// ===========================================================================

// ===========================================================================
// 8. Kernel provider
// ===========================================================================

TEST_F(SealighterSession_ParseConfig_Test, KernelProvider_Invalid) {
    std::string config =
        "{"
        R"("session_properties": {},)"
        R"("kernel_traces": [{)"
        R"("provider_name": "nonexistent",)"
        R"("trace_name": "test",)"
        R"("buffers": [])"
        "}]"
        "}";
    int status = session.parse_config(config);
    EXPECT_EQ(status, SEALIGHTER_ERROR_PARSE_KERNEL_PROVIDER);
}

TEST_F(SealighterSession_ParseConfig_Test, KernelProvider_MissingName) {
    // kernel_traces entry without provider_name -> error 6
    std::string config =
        "{"
        R"("session_properties": {},)"
        R"("kernel_traces": [{)"
        R"("trace_name": "test",)"
        R"("buffers": [])"
        "}]"
        "}";
    int status = session.parse_config(config);
    EXPECT_EQ(status, SEALIGHTER_ERROR_PARSE_KERNEL_PROVIDER);
}
