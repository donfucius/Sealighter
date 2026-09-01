#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <gtest/gtest.h>
#include "sealighter_handler.h"
#include "sealighter_errors.h"

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

// ---------------------------------------------------------------------------
// 1. Setter tests
// ---------------------------------------------------------------------------

TEST(SealighterSession_Setters, SetOutputFormat_DoesNotCrash) {
    SealighterSession session;
    session.set_output_format(output_stdout);
    session.set_output_format(output_event_log);
    session.set_output_format(output_file);
    // Reaching this point without crashing is the assertion.
    SUCCEED();
}

TEST(SealighterSession_Setters, SetBufferListsTimeout_DoesNotCrash) {
    SealighterSession session;
    session.set_buffer_lists_timeout(0);
    session.set_buffer_lists_timeout(1);
    session.set_buffer_lists_timeout(5);
    session.set_buffer_lists_timeout(3600);
    session.set_buffer_lists_timeout(UINT32_MAX);
    SUCCEED();
}

TEST(SealighterSession_Setters, AddBufferedList_SingleTrace) {
    SealighterSession session;
    event_buffer_list_t list(1, 10);
    session.add_buffered_list("trace_a", std::move(list));
    SUCCEED();
}

TEST(SealighterSession_Setters, AddBufferedList_MultipleTraces) {
    SealighterSession session;
    session.add_buffered_list("trace_a", event_buffer_list_t(1, 10));
    session.add_buffered_list("trace_b", event_buffer_list_t(2, 20));
    session.add_buffered_list("trace_c", event_buffer_list_t(3, 30));
    SUCCEED();
}

TEST(SealighterSession_Setters, AddBufferedList_SameTraceMultipleLists) {
    SealighterSession session;
    session.add_buffered_list("trace_a", event_buffer_list_t(1, 10));
    session.add_buffered_list("trace_a", event_buffer_list_t(2, 20));
    session.add_buffered_list("trace_a", event_buffer_list_t(3, 30));
    SUCCEED();
}

// ---------------------------------------------------------------------------
// 2. File I/O tests
// ---------------------------------------------------------------------------

TEST(SealighterSession_FileIO, SetupLoggerFile_ValidPath) {
    SealighterSession session;

    auto log_path = std::filesystem::temp_directory_path() / "sealighter_test_setup_valid.log";
    std::string path_str = log_path.string();

    int result = session.setup_logger_file(path_str);
    EXPECT_EQ(result, ERROR_SUCCESS);

    // Clean up
    session.teardown_logger_file();
    std::error_code ec;
    std::filesystem::remove(log_path, ec);
}

TEST(SealighterSession_FileIO, SetupLoggerFile_InvalidPath) {
    SealighterSession session;

    std::string bad_path = "Z:\\nonexistent\\dir\\file.log";
    int result = session.setup_logger_file(bad_path);
    EXPECT_EQ(result, SEALIGHTER_ERROR_OUTPUT_FILE);
}

TEST(SealighterSession_FileIO, TeardownLoggerFile_AfterSetup) {
    SealighterSession session;

    auto log_path = std::filesystem::temp_directory_path() / "sealighter_test_teardown_after.log";
    std::string path_str = log_path.string();

    int result = session.setup_logger_file(path_str);
    EXPECT_EQ(result, ERROR_SUCCESS);

    session.teardown_logger_file();
    SUCCEED();

    // Clean up
    std::error_code ec;
    std::filesystem::remove(log_path, ec);
}

TEST(SealighterSession_FileIO, TeardownLoggerFile_WithoutSetup) {
    SealighterSession session;
    session.teardown_logger_file();
    // Should not crash even if no file was ever set up.
    SUCCEED();
}

// ---------------------------------------------------------------------------
// 3. Buffering lifecycle tests
// ---------------------------------------------------------------------------

TEST(SealighterSession_Buffering, StartBuffering_NoLists) {
    SealighterSession session;
    // No buffer lists added -- starting should be a safe no-op.
    session.start_bufferring();
    session.stop_bufferring();
    SUCCEED();
}

TEST(SealighterSession_Buffering, StartAndStopBuffering) {
    SealighterSession session;
    session.add_buffered_list("trace_a", event_buffer_list_t(1, 10));
    session.set_buffer_lists_timeout(1);

    session.start_bufferring();
    // Allow the thread a moment to start.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    session.stop_bufferring();
    SUCCEED();
}

TEST(SealighterSession_Buffering, StopBuffering_WithoutStart) {
    SealighterSession session;
    session.add_buffered_list("trace_a", event_buffer_list_t(1, 10));
    // Stopping without ever starting throws an exception
    EXPECT_ANY_THROW(session.stop_bufferring());
}

// ---------------------------------------------------------------------------
// 4. Event worker lifecycle tests
// ---------------------------------------------------------------------------

TEST(SealighterSession_Worker, StartStopEventProcessing_NoTraces) {
    SealighterSession session;
    session.start_event_processing();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    session.stop_event_processing();
    SUCCEED();
}

TEST(SealighterSession_Worker, StopEventProcessing_WithoutStart_DoesNotThrow) {
    SealighterSession session;
    session.stop_event_processing();
    SUCCEED();
}
