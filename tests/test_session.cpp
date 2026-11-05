#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <gtest/gtest.h>
#include "sealighter_handler.h"
#include "sealighter_errors.h"

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <winevt.h>
#include "sealighter_provider.h"
#pragma comment(lib, "wevtapi.lib")

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

#include <krabs.hpp>
#include "sealighter_util.h"

namespace {

// Microsoft-Windows-PowerShell event 7942 version 1 declares eight
// UnicodeString properties; sealighter decodes every one of them when the
// record is packed, which makes it a clean-record fixture.
constexpr const wchar_t* kPowerShellGuid = L"{A0C1853B-5C40-4B15-8766-3CF1C58F985A}";
constexpr DWORD kPowerShellEventId = 7942;
constexpr DWORD kPowerShellEventVersion = 1;

json shape_record(SealighterSession& session, bool fill_all_properties)
{
    krabs::testing::record_builder builder(
        krabs::guid(kPowerShellGuid), kPowerShellEventId, kPowerShellEventVersion);

    if (fill_all_properties) {
        builder.add_properties()
            (L"ClassName", L"cn")
            (L"MethodName", L"mn")
            (L"WorkflowGuid", L"wg")
            (L"Message", L"ms")
            (L"JobData", L"jd")
            (L"ActivityName", L"an")
            (L"ActivityGuid", L"ag")
            (L"Parameters", L"pa");
    }

    auto record = fill_all_properties ? builder.pack() : builder.pack_incomplete();
    krabs::trace_context ctx;
    krabs::schema schema(record, ctx.schema_locator);
    auto context = std::make_shared<sealighter_context_t>("shape_test", false);

    return session.parse_event_to_json(record, ctx, context, schema);
}

const char* const kAllHeaderKeys[] = {
    "activity_id", "event_flags", "event_id", "event_name", "event_opcode",
    "event_version", "process_id", "provider_name", "task_name", "thread_id",
    "timestamp", "trace_name"
};

} // namespace

// Mutate only a test-local TDH cache, not the installed provider schema. A
// two-byte INT32 deterministically throws in both Debug and Release; hex fallback
// can still read those bytes. The last field leaves all earlier fields intact.
TEST(SealighterSession_RecordShape, DecodeFailuresRemainVisibleWithoutTypeMap) {
    krabs::testing::record_builder builder(
        krabs::guid(kPowerShellGuid), kPowerShellEventId, kPowerShellEventVersion);
    builder.add_properties()(L"ClassName", L"cn")(L"MethodName", L"mn")
        (L"WorkflowGuid", L"wg")(L"Message", L"ms")(L"JobData", L"jd")
        (L"ActivityName", L"an")(L"ActivityGuid", L"ag")(L"Parameters", L"pa");
    auto record = builder.pack();
    krabs::trace_context ctx;
    auto info = ctx.schema_locator.get_event_schema(record);
    ASSERT_EQ(info->PropertyCount, 8u);
    auto& field = info->EventPropertyInfoArray[7];
    field.Flags = static_cast<PROPERTY_FLAGS>(0);
    field.length = 2;
    auto context = std::make_shared<sealighter_context_t>("shape_test", false);

    for (USHORT type : { USHORT(TDH_INTYPE_INT32), USHORT(TDH_INTYPE_HEXINT32) }) {
        SCOPED_TRACE(type);
        field.nonStructType.InType = type;
        krabs::schema schema(record, ctx.schema_locator);
        SealighterSession full;
        json baseline = full.parse_event_to_json(record, ctx, context, schema);
        EXPECT_EQ(baseline["property_types"]["Parameters"],
            type == TDH_INTYPE_INT32 ? "ERROR" : "OTHER");
        EXPECT_FALSE(baseline.contains("parse_errors"));

        SealighterSession slim;
        slim.set_omit_property_types(true);
        slim.set_record_parse_errors(true);
        json event = slim.parse_event_to_json(record, ctx, context, schema);
        EXPECT_EQ(event["parse_errors"], json::array({"Parameters"}));
        EXPECT_EQ(event["properties"], baseline["properties"]);
        EXPECT_EQ(event["properties"]["ClassName"], "cn");
        EXPECT_FALSE(event.contains("property_types"));
        const auto line = convert_json_string(event, false);
        EXPECT_EQ(line.find('\n'), std::string::npos);
        EXPECT_EQ(json::parse(line), event);
    }
}

TEST(SealighterSession_RecordShape, SameRecordsKeepValuesAndOrderWithFewerBytes) {
    SealighterSession full, slim;
    slim.set_omit_property_types(true);
    slim.set_record_parse_errors(true);
    slim.set_omit_header_fields({"activity_id", "event_name", "event_flags"});
    size_t full_bytes = 0, slim_bytes = 0;
    for (int i = 0; i != 32; ++i) {
        // Both sessions receive the very same record, including its timestamp.
        krabs::testing::record_builder builder(
            krabs::guid(kPowerShellGuid), kPowerShellEventId, kPowerShellEventVersion);
        builder.add_properties()(L"ClassName", std::to_wstring(i));
        auto record = builder.pack_incomplete();
        krabs::trace_context ctx;
        krabs::schema schema(record, ctx.schema_locator);
        auto context = std::make_shared<sealighter_context_t>("shape_test", false);
        auto before = full.parse_event_to_json(record, ctx, context, schema);
        auto after = slim.parse_event_to_json(record, ctx, context, schema);
        full_bytes += convert_json_string(before, false).size();
        slim_bytes += convert_json_string(after, false).size();
        before.erase("property_types");
        for (auto key : OMITTABLE_HEADER_FIELDS) before["header"].erase(std::string(key));
        EXPECT_EQ(after, before);
        EXPECT_EQ(after["properties"]["ClassName"], std::to_string(i));
    }
    EXPECT_LT(slim_bytes, full_bytes);
}

// ---------------------------------------------------------------------------
// 5. Record shape: defaults keep today's keys
// ---------------------------------------------------------------------------

TEST(SealighterSession_RecordShape, DefaultRecordKeepsEveryCurrentKey) {
    SealighterSession session;
    json event = shape_record(session, true);

    EXPECT_TRUE(event.contains("header"));
    EXPECT_TRUE(event.contains("properties"));
    EXPECT_TRUE(event.contains("property_types"));
    EXPECT_FALSE(event.contains("parse_errors"));

    for (const char* key : kAllHeaderKeys) {
        EXPECT_TRUE(event["header"].contains(key)) << key;
    }
    EXPECT_EQ(event["header"]["trace_name"].get<std::string>(), "shape_test");
    EXPECT_FALSE(event["properties"].empty());
    // Millisecond precision is the default; the synthetic record's timestamp is
    // the ETW epoch, so the expected string is exact.
    EXPECT_EQ(event["header"]["timestamp"].get<std::string>(), "1601-01-01 00:00:00.000Z");
}

TEST(SealighterSession_RecordShape, SecondsPrecisionIsStillSelectable) {
    SealighterSession session;
    session.set_timestamp_precision(Timestamp_precision::seconds);
    json event = shape_record(session, true);

    EXPECT_EQ(event["header"]["timestamp"].get<std::string>(), "1601-01-01 00:00:00Z");
}

// ---------------------------------------------------------------------------
// 6. Type map reduction
// ---------------------------------------------------------------------------

TEST(SealighterSession_RecordShape, OmitPropertyTypesRemovesOnlyTheTypeMap) {
    SealighterSession session;
    session.set_omit_property_types(true);
    json event = shape_record(session, true);

    EXPECT_FALSE(event.contains("property_types"));
    EXPECT_FALSE(event["properties"].empty());
    // Independent switches: the header is untouched by this option.
    for (const char* key : kAllHeaderKeys) {
        EXPECT_TRUE(event["header"].contains(key)) << key;
    }
}

// ---------------------------------------------------------------------------
// 7. Header reduction
// ---------------------------------------------------------------------------

TEST(SealighterSession_RecordShape, OmitHeaderFieldsRemovesExactlyThoseKeys) {
    SealighterSession session;
    session.set_omit_header_fields({ "activity_id", "event_name", "event_flags" });
    json event = shape_record(session, true);

    EXPECT_FALSE(event["header"].contains("activity_id"));
    EXPECT_FALSE(event["header"].contains("event_name"));
    EXPECT_FALSE(event["header"].contains("event_flags"));

    for (const char* key : kAllHeaderKeys) {
        const std::string name(key);
        if (name == "activity_id" || name == "event_name" || name == "event_flags") {
            continue;
        }
        EXPECT_TRUE(event["header"].contains(key)) << key;
    }
}

TEST(SealighterSession_RecordShape, OmittedRecordStaysSingleLineValidJson) {
    SealighterSession session;
    session.set_omit_property_types(true);
    session.set_omit_header_fields({ "activity_id", "event_name", "event_flags" });
    json event = shape_record(session, true);

    const std::string line = convert_json_string(event, false);
    EXPECT_EQ(line.find('\n'), std::string::npos);

    json round_trip;
    EXPECT_NO_THROW(round_trip = json::parse(line));
    EXPECT_EQ(round_trip["header"]["provider_name"].get<std::string>(), "Microsoft-Windows-PowerShell");
    EXPECT_FALSE(round_trip.contains("property_types"));
}

// ---------------------------------------------------------------------------
// 8. parse_errors on a record that decoded cleanly
// ---------------------------------------------------------------------------

TEST(SealighterSession_RecordShape, CleanRecordCarriesNoParseErrorsKey) {
    SealighterSession session;
    session.set_record_parse_errors(true);
    json event = shape_record(session, true);

    EXPECT_FALSE(event.contains("parse_errors"));
    EXPECT_EQ(event["properties"]["ClassName"].get<std::string>(), "cn");
}

TEST(SealighterSession_RecordShape, ConfiguredOptionsReachTheRecord) {
    // Proves the config keys land on the session, not just that they parse.
    SealighterSession session;
    const std::string config =
        R"({"session_properties": {"output_format": "stdout",)"
        R"("omit_property_types": true,)"
        R"("omit_header_fields": ["activity_id"],)"
        R"("timestamp_precision": "ms"},)"
        R"("kernel_traces": [{"provider_name": "process", "trace_name": "shape_test", "buffers": []}]})";
    ASSERT_EQ(session.parse_config(config), ERROR_SUCCESS);

    json event = shape_record(session, true);

    EXPECT_FALSE(event.contains("property_types"));
    EXPECT_FALSE(event["header"].contains("activity_id"));

    const std::string timestamp = event["header"]["timestamp"].get<std::string>();
    EXPECT_EQ(timestamp.size(), 24u);   // "YYYY-MM-DD HH:MM:SS.mmmZ"
    EXPECT_EQ(timestamp[19], '.');
    EXPECT_EQ(timestamp[23], 'Z');
}

// ---------------------------------------------------------------------------
// 9. event_log output with the reductions enabled
// ---------------------------------------------------------------------------

TEST(SealighterSession_EventLogFields, PresentKeysPassThrough) {
    json event = { {"header", {
        {"activity_id", "{12345678-ABCD-EF01-2345-6789ABCDEF01}"},
        {"event_flags", 576},
        {"event_id", 10},
        {"event_name", "name"},
        {"event_opcode", 3},
        {"event_version", 2},
        {"process_id", 4242},
        {"provider_name", "Some-Provider"},
        {"task_name", "Task"},
        {"thread_id", 13},
    }}};

    json fields = SealighterSession::event_log_fields(event);

    EXPECT_EQ(fields["activity_id"].get<std::string>(), "{12345678-ABCD-EF01-2345-6789ABCDEF01}");
    EXPECT_EQ(fields["event_flags"].get<std::uint32_t>(), 576u);
    EXPECT_EQ(fields["event_id"].get<std::uint32_t>(), 10u);
    EXPECT_EQ(fields["event_name"].get<std::string>(), "name");
    EXPECT_EQ(fields["event_opcode"].get<std::uint32_t>(), 3u);
    EXPECT_EQ(fields["event_version"].get<std::uint32_t>(), 2u);
    EXPECT_EQ(fields["process_id"].get<std::uint32_t>(), 4242u);
    EXPECT_EQ(fields["provider_name"].get<std::string>(), "Some-Provider");
    EXPECT_EQ(fields["task_name"].get<std::string>(), "Task");
    EXPECT_EQ(fields["thread_id"].get<std::uint32_t>(), 13u);
}

TEST(SealighterSession_EventLogFields, OmittedKeysFallBackToDefaults) {
    // activity_id, event_name and event_flags are the only keys a reduction
    // may remove, so they are the only ones with a documented default.
    json event = { {"header", {
        {"event_id", 10},
        {"event_opcode", 3},
        {"event_version", 2},
        {"process_id", 4242},
        {"provider_name", "Some-Provider"},
        {"task_name", "Task"},
        {"thread_id", 13},
        {"timestamp", "2020-01-15 12:30:45.123Z"},
        {"trace_name", "shape_test"},
    }}};

    json fields = SealighterSession::event_log_fields(event);

    EXPECT_EQ(fields["activity_id"].get<std::string>(), "");
    EXPECT_EQ(fields["event_name"].get<std::string>(), "");
    EXPECT_EQ(fields["event_flags"].get<std::uint32_t>(), 0u);
    EXPECT_EQ(fields["event_id"].get<std::uint32_t>(), 10u);
    EXPECT_EQ(fields["event_opcode"].get<std::uint32_t>(), 3u);
    EXPECT_EQ(fields["event_version"].get<std::uint32_t>(), 2u);
    EXPECT_EQ(fields["process_id"].get<std::uint32_t>(), 4242u);
    EXPECT_EQ(fields["provider_name"].get<std::string>(), "Some-Provider");
    EXPECT_EQ(fields["task_name"].get<std::string>(), "Task");
    EXPECT_EQ(fields["thread_id"].get<std::uint32_t>(), 13u);
}

TEST(SealighterSession_RecordShape, EveryReductionKeepsEventLogPathAlive) {
    SealighterSession session;
    session.set_output_format(output_event_log);
    session.set_omit_property_types(true);
    session.set_record_parse_errors(true);
    session.set_omit_header_fields({ "activity_id", "event_name", "event_flags" });
    session.set_timestamp_precision(Timestamp_precision::milliseconds);

    json event = shape_record(session, true);
    for (const char* key : { "activity_id", "event_name", "event_flags" }) {
        EXPECT_FALSE(event["header"].contains(key)) << key;
    }

    const std::string line = convert_json_string(event, false);
    // The keys the record shape pins must survive every reduction, or the
    // Event Log loses its trace identity and the routing read breaks.
    for (const char* key : { "trace_name", "timestamp", "process_id", "task_name" }) {
        EXPECT_TRUE(event["header"].contains(key)) << key;
    }

    EXPECT_NO_THROW(session.write_event_log(event, "shape_test", line));
}

// Opt in explicitly: an unavailable/disabled channel must fail acceptance,
// while ordinary unit tests must not require machine-wide manifest installation.
TEST(SealighterSession_EventLogIntegration, PublishedJsonReachesChannel) {
    if (GetEnvironmentVariableA("SEALIGHTER_EVENT_LOG_TEST", nullptr, 0) == 0) {
        GTEST_SKIP() << "Set SEALIGHTER_EVENT_LOG_TEST=1 with Sealighter/Operational installed";
    }
    struct Handle {
        EVT_HANDLE value;
        ~Handle() { if (value) EvtClose(value); }
    };
    Handle channel{EvtOpenLog(nullptr, L"Sealighter/Operational", EvtOpenChannelPath)};
    ASSERT_NE(channel.value, nullptr) << GetLastError();
    ASSERT_EQ(EventRegisterSealighter(), ERROR_SUCCESS);
    struct Registration { ~Registration() { EventUnregisterSealighter(); } } registration;

    SealighterSession session;
    session.set_omit_property_types(true);
    session.set_record_parse_errors(true);
    session.set_omit_header_fields({"activity_id", "event_name", "event_flags"});
    session.set_timestamp_precision(Timestamp_precision::milliseconds);
    json event = shape_record(session, true);
    const std::string marker = "slim_acceptance_" + std::to_string(GetCurrentProcessId()) +
        "_" + std::to_string(GetTickCount64());
    event["header"]["trace_name"] = marker;
    const auto line = convert_json_string(event, false);
    // Registration alone does not prove delivery. Query the channel for this
    // unique trace marker and compare the actual JSON payload after publication.
    session.write_event_log(event, marker, line);
    const auto query = L"*[EventData[Data[@Name='trace_name']='" +
        convert_str_wstr(marker) + L"']]";
    Handle render_context{EvtCreateRenderContext(0, nullptr, EvtRenderContextUser)};
    ASSERT_NE(render_context.value, nullptr) << GetLastError();
    for (int attempt = 0; attempt < 50; ++attempt) {
        Handle results{EvtQuery(nullptr, L"Sealighter/Operational", query.c_str(),
            EvtQueryChannelPath | EvtQueryReverseDirection)};
        ASSERT_NE(results.value, nullptr) << GetLastError();
        EVT_HANDLE found = nullptr;
        DWORD count = 0;
        if (EvtNext(results.value, 1, &found, 0, 0, &count)) {
            Handle received{found};
            DWORD bytes = 0, properties = 0;
            EvtRender(render_context.value, found, EvtRenderEventValues, 0, nullptr, &bytes, &properties);
            ASSERT_EQ(GetLastError(), ERROR_INSUFFICIENT_BUFFER);
            std::vector<ULONGLONG> storage((bytes + sizeof(ULONGLONG) - 1) / sizeof(ULONGLONG));
            ASSERT_TRUE(EvtRender(render_context.value, found, EvtRenderEventValues,
                static_cast<DWORD>(storage.size() * sizeof(ULONGLONG)), storage.data(), &bytes, &properties))
                << GetLastError();
            ASSERT_GE(properties, 1u);
            const auto* values = reinterpret_cast<const EVT_VARIANT*>(storage.data());
            ASSERT_EQ(values[0].Type, DWORD(EvtVarTypeString));
            EXPECT_EQ(json::parse(convert_wstr_str(values[0].StringVal)), event);
            return;
        }
        ASSERT_EQ(GetLastError(), ERROR_NO_MORE_ITEMS);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    FAIL() << "No event with trace marker " << marker << " reached Sealighter/Operational";
}
