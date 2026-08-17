#include "sealighter_krabs.h"
#include "sealighter_handler.h"
#include "sealighter_errors.h"
#include "sealighter_util.h"
#include "sealighter_provider.h"

#include "logger.h"
#define loggr (logger::Logger::GetInstance().logger())

#include <atomic>
#include <chrono>
#include <fstream>
#include <mutex>

// -------------------------
// PRIVATE FUNCTIONS - START
// -------------------------


/*
    Print a line to stdout, using a mutex
    to ensure we print each event wholey before
    another can
*/
void SealighterSession::threaded_print_ln
(
    const std::string& event_string
)
{
    std::lock_guard<std::mutex> lock(print_mutex_);
    loggr.info("{}", event_string.c_str());
}


/*
    Write to Event Log
*/
void SealighterSession::write_event_log
(
    const json&         json_event,
    const std::string&  trace_name,
    const std::string&  event_string
)
{
    DWORD status = ERROR_SUCCESS;

    // TODO: Make sure we didn't break this
    // Also fix up schema, no need to to all the str_wstr converting
    // Also fix up timestamp string
    status = EventWriteSEALIGHTER_REPORT_EVENT(
        event_string.c_str(),
        json_event["header"]["activity_id"].get<std::string>().c_str(),
        (USHORT)json_event["header"]["event_flags"].get<std::uint32_t>(),
        (USHORT)json_event["header"]["event_id"].get<std::uint32_t>(),
        convert_str_wstr(json_event["header"]["event_name"].get<std::string>()).c_str(),
        (UCHAR)json_event["header"]["event_opcode"].get<std::uint32_t>(),
        (UCHAR)json_event["header"]["event_version"].get<std::uint32_t>(),
        json_event["header"]["process_id"].get<std::uint32_t>(),
        convert_str_wstr(json_event["header"]["provider_name"].get<std::string>()).c_str(),
        convert_str_wstr(json_event["header"]["task_name"].get<std::string>()).c_str(),
        json_event["header"]["thread_id"].get<std::uint32_t>(),
        0,  // schema.timestamp().quadPart
        trace_name.c_str()
    );

    if (status != ERROR_SUCCESS) {
        loggr.info("Error {} line {}", status, __LINE__);
        return;
    }
}


/*
    Print a line to an output file, using a mutex
    to ensure we print each event wholey before
    another can
*/
void SealighterSession::threaded_write_file_ln
(
    const std::string& event_string
)
{
    std::lock_guard<std::mutex> lock(print_mutex_);
    outfile_ << event_string << std::endl;
}


static void parse_extended_data(const EVENT_RECORD& record, json& json_event)
{
    if (record.ExtendedDataCount == 0) {
        return;
    }
    for (USHORT i = 0; i < record.ExtendedDataCount; i++)
    {
        EVENT_HEADER_EXTENDED_DATA_ITEM data_item = record.ExtendedData[i];
        if (data_item.ExtType == EVENT_HEADER_EXT_TYPE_STACK_TRACE64) {
            PEVENT_EXTENDED_ITEM_STACK_TRACE64 stacktrace =
                (PEVENT_EXTENDED_ITEM_STACK_TRACE64)data_item.DataPtr;
            uint32_t stack_length = (data_item.DataSize - sizeof(ULONG64)) / sizeof(ULONG64);

            json json_stacktrace = json::array();
            for (size_t x = 0; x < stack_length; x++)
            {
                json_stacktrace.push_back(convert_ulong64_hexstring(stacktrace->Address[x]));
            }
            json_event["stack_trace"] = json_stacktrace;
        }
    }
}


/*
    Convert an ETW Event to JSON
*/
json SealighterSession::parse_event_to_json
(
    const EVENT_RECORD& record,
    const krabs::trace_context&,
    std::shared_ptr<struct sealighter_context_t> sealighter_context,
    krabs::schema       schema
)
{
    std::string trace_name = sealighter_context->trace_name;
    json json_properties;
    json json_properties_types;
    json json_header = {
        { "event_id", schema.event_id() },
        { "event_name", convert_wstr_str(schema.event_name()) },
        { "task_name", convert_wstr_str(schema.task_name()) },
        { "thread_id", schema.thread_id() },
        { "timestamp", convert_timestamp_string(schema.timestamp()) },
        { "event_flags", schema.event_flags() },
        { "event_opcode", schema.event_opcode() },
        { "event_version", schema.event_version() },
        { "process_id", schema.process_id()},
        { "provider_name", convert_wstr_str(schema.provider_name()) },
        { "activity_id", convert_guid_str(schema.activity_id()) },
        { "trace_name", trace_name},
    };

    json json_event = { {"header", json_header} };

    // Check if we are just dumping the raw event, or attempting to parse it
    if (sealighter_context->dump_raw_event) {
        std::string raw_hex = convert_bytearray_hexstring((BYTE*)record.UserData, record.UserDataLength);
        json_event["raw"] = raw_hex;
    }
    else {
        krabs::parser parser(schema);
        for (krabs::property& prop : parser.properties()) {
            std::wstring prop_name_wstr = prop.name();
            std::string prop_name = convert_wstr_str(prop_name_wstr);

            try
            {
                switch (prop.type())
                {
                case TDH_INTYPE_ANSISTRING:
                    json_properties[prop_name] = parser.parse<std::string>(prop_name_wstr);
                    json_properties_types[prop_name] = "STRINGA";
                    break;
                case TDH_INTYPE_UNICODESTRING:
                    json_properties[prop_name] = convert_wstr_str(parser.parse<std::wstring>(prop_name_wstr));
                    json_properties_types[prop_name] = "STRINGW";
                    break;
                case TDH_INTYPE_INT8:
                    json_properties[prop_name] = parser.parse<std::int8_t>(prop_name_wstr);
                    json_properties_types[prop_name] = "INT8";
                    break;
                case TDH_INTYPE_UINT8:
                    json_properties[prop_name] = parser.parse<std::uint8_t>(prop_name_wstr);
                    json_properties_types[prop_name] = "UINT8";
                    break;
                case TDH_INTYPE_INT16:
                    json_properties[prop_name] = parser.parse<std::int16_t>(prop_name_wstr);
                    json_properties_types[prop_name] = "INT16";
                    break;
                case TDH_INTYPE_UINT16:
                    json_properties[prop_name] = parser.parse<std::uint16_t>(prop_name_wstr);
                    json_properties_types[prop_name] = "UINT16";
                    break;
                case TDH_INTYPE_INT32:
                    json_properties[prop_name] = parser.parse<std::int32_t>(prop_name_wstr);
                    json_properties_types[prop_name] = "INT32";
                    break;
                case TDH_INTYPE_UINT32:
                    json_properties[prop_name] = parser.parse<std::uint32_t>(prop_name_wstr);
                    json_properties_types[prop_name] = "UINT32";
                    break;
                case TDH_INTYPE_INT64:
                    json_properties[prop_name] = parser.parse<std::int64_t>(prop_name_wstr);
                    json_properties_types[prop_name] = "INT64";
                    break;
                case TDH_INTYPE_UINT64:
                    json_properties[prop_name] = parser.parse<std::uint64_t>(prop_name_wstr);
                    json_properties_types[prop_name] = "UINT64";
                    break;
                case TDH_INTYPE_FLOAT:
                    json_properties[prop_name] = parser.parse<std::float_t>(prop_name_wstr);
                    json_properties_types[prop_name] = "FLOAT";
                    break;
                case TDH_INTYPE_DOUBLE:
                    json_properties[prop_name] = parser.parse<std::double_t>(prop_name_wstr);
                    json_properties_types[prop_name] = "DOUBLE";
                    break;
                case TDH_INTYPE_BOOLEAN:
                    json_properties[prop_name] = convert_bytes_bool(parser.parse<krabs::binary>(prop_name_wstr).bytes());
                    json_properties_types[prop_name] = "BOOLEAN";
                    break;
                case TDH_INTYPE_BINARY:
                    json_properties[prop_name] =
                        convert_bytevector_hexstring(parser.parse<krabs::binary>(prop_name_wstr).bytes());
                    json_properties_types[prop_name] = "BINARY";
                    break;
                case TDH_INTYPE_GUID:
                    json_properties[prop_name] =
                        convert_guid_str(parser.parse<krabs::guid>(prop_name_wstr));
                    json_properties_types[prop_name] = "GUID";
                    break;
                case TDH_INTYPE_FILETIME:
                    json_properties[prop_name] = convert_filetime_string(
                        parser.parse<FILETIME>(prop_name_wstr));
                    json_properties_types[prop_name] = "FILETIME";
                    break;
                case TDH_INTYPE_SYSTEMTIME:
                    json_properties[prop_name] = convert_systemtime_string(
                        parser.parse<SYSTEMTIME>(prop_name_wstr));
                    json_properties_types[prop_name] = "SYSTEMTIME";
                    break;
                case TDH_INTYPE_SID:
                    json_properties[prop_name] = convert_bytes_sidstring(
                        parser.parse<krabs::binary>(prop_name_wstr).bytes());
                    json_properties_types[prop_name] = "SID";
                    break;
                case TDH_INTYPE_WBEMSID:
                    // *Supposedly* like SID?
                    json_properties[prop_name] = convert_bytevector_hexstring(
                        parser.parse<krabs::binary>(prop_name_wstr).bytes());
                    json_properties_types[prop_name] = "WBEMSID";
                    break;
                case TDH_INTYPE_POINTER:
                    json_properties[prop_name] =
                        convert_ulong64_hexstring(parser.parse<krabs::pointer>(prop_name_wstr).address);
                    json_properties_types[prop_name] = "POINTER";
                    break;
                case TDH_INTYPE_HEXINT32:
                case TDH_INTYPE_HEXINT64:
                case TDH_INTYPE_MANIFEST_COUNTEDSTRING:
                case TDH_INTYPE_MANIFEST_COUNTEDANSISTRING:
                case TDH_INTYPE_RESERVED24:
                case TDH_INTYPE_MANIFEST_COUNTEDBINARY:
                case TDH_INTYPE_COUNTEDSTRING:
                case TDH_INTYPE_COUNTEDANSISTRING:
                case TDH_INTYPE_REVERSEDCOUNTEDSTRING:
                case TDH_INTYPE_REVERSEDCOUNTEDANSISTRING:
                case TDH_INTYPE_NONNULLTERMINATEDSTRING:
                case TDH_INTYPE_NONNULLTERMINATEDANSISTRING:
                case TDH_INTYPE_UNICODECHAR:
                case TDH_INTYPE_ANSICHAR:
                case TDH_INTYPE_SIZET:
                case TDH_INTYPE_HEXDUMP:
                case TDH_INTYPE_NULL:
                default:
                    json_properties[prop_name] =
                        convert_bytevector_hexstring(parser.parse<krabs::binary>(prop_name_wstr).bytes());
                    json_properties_types[prop_name] = "OTHER";
                    break;
                }
            }
            catch (...)
            {
                // Failed to parse, default to hex
                // Try hex, if something even worse is up return empty
                try
                {
                    json_properties[prop_name] =
                        convert_bytevector_hexstring(parser.parse<krabs::binary>(prop_name_wstr).bytes());
                    json_properties_types[prop_name] = "ERROR";
                }
                catch (...) {}
            }
        }
        json_event["property_types"] = json_properties_types;
        json_event["properties"] = json_properties;
    }

    parse_extended_data(record, json_event);

    return json_event;
}

void SealighterSession::output_json_event
(
    const json& json_event
)
{
    // If writing to a file, don't pretty print
    // This makes it 1 line per event
    bool pretty_print = (Output_format::output_file != output_format_);
    std::string event_string = convert_json_string(json_event, pretty_print);
    std::string trace_name = json_event["header"]["trace_name"];

    // Log event if we successfully parsed it
    if (!event_string.empty()) {
        switch (output_format_)
        {
        case output_stdout:
            threaded_print_ln(event_string);
            break;
        case output_event_log:
            write_event_log(json_event, trace_name, event_string);
            break;
        case output_file:
            threaded_write_file_ln(event_string);
            break;
        }
    }
}

void SealighterSession::handle_event_context
(
    const EVENT_RECORD& record,
    const krabs::trace_context& trace_context,
    std::shared_ptr<struct sealighter_context_t> sealighter_context
)
{
    json json_event;
    krabs::schema schema(record, trace_context.schema_locator);
    bool buffered = false;

    std::string trace_name = sealighter_context->trace_name;
    json_event = parse_event_to_json(record, trace_context, sealighter_context, schema);

    // Only care about event buffering if required
    {
        std::lock_guard<std::mutex> lock(buffer_lists_mutex_);
        if (buffer_lists_.size() > 0 && buffer_lists_.find(trace_name) != buffer_lists_.end()) {
            for (event_buffer_list_t& buffer : buffer_lists_[trace_name]) {
                if (buffer.event_id != (uint32_t)schema.event_id()) {
                    continue;
                }
                if (buffer.event_count < buffer.max_before_buffering) {
                    // Increment counter but report event
                    buffer.event_count += 1;
                    break;
                }

                // We're buffering. See if we already have the matching event
                bool matched_event = false;
                for (json& json_event_buffered : buffer.json_event_buffered) {
                    bool matched_field = true;
                    for (std::string prop_to_compare: buffer.properties_to_compare) {
                        auto field_event = convert_json_string(json_event["properties"][prop_to_compare], false);
                        auto field_buffered = convert_json_string(json_event_buffered["properties"][prop_to_compare], false);
                        if (field_event != field_buffered) {
                            // Not a match
                            matched_field = false;
                            break;
                        }
                    }
                    if (matched_field) {
                        // Matched, increase event count
                        auto old_count = json_event_buffered["header"]["buffered_count"].get<std::uint32_t>();
                        json_event_buffered["header"]["buffered_count"] = old_count + 1;
                        matched_event = true;
                    }
                }
                if (!matched_event) {
                    // Event wasn't in the list, add it
                    json_event["header"]["buffered_count"] = 1;
                    buffer.json_event_buffered.push_back(json_event);
                }
                // As we're buffering don't report event
                buffered = true;
                break;
            }
        }
    }

    // Report event only if not buffering
    if (!buffered) {
        output_json_event(json_event);
    }
}



void SealighterSession::handle_event
(
    const EVENT_RECORD& record,
    const krabs::trace_context& trace_context
)
{
    auto dummy_context = std::make_shared<struct sealighter_context_t>("", false);
    this->handle_event_context(record, trace_context, dummy_context);
}

int SealighterSession::setup_logger_file
(
    const std::string& filename
)
{
    outfile_.open(filename.c_str(), std::ios::out | std::ios::trunc);
    if (outfile_.good()) {
        return ERROR_SUCCESS;
    }
    else {
        return SEALIGHTER_ERROR_OUTPUT_FILE;
    }
}

void SealighterSession::teardown_logger_file()
{
    if (outfile_.is_open()) {
        outfile_.close();
    }
}


void SealighterSession::set_output_format(Output_format format)
{
    output_format_ = format;
}


void SealighterSession::add_buffered_list
(
    const std::string& trace_name,
    event_buffer_list_t buffered_list
)
{
    if (buffer_lists_.find(trace_name) == buffer_lists_.end()) {
        buffer_lists_[trace_name] = std::vector<event_buffer_list_t>();
    }
    buffer_lists_[trace_name].push_back(buffered_list);
}

void SealighterSession::set_buffer_lists_timeout
(
    uint32_t timeout
)
{
    buffer_lists_timeout_seconds_ = timeout;
}

void SealighterSession::flush_buffered_lists()
{
    std::lock_guard<std::mutex> lock(buffer_lists_mutex_);
    for (auto& buffer_list : buffer_lists_) {
        for (auto& buffer : buffer_list.second) {
            for (auto& json_event : buffer.json_event_buffered) {
                output_json_event(json_event);
            }
            buffer.json_event_buffered.clear();
            buffer.event_count = 0;
        }
    }
}

// ...

void SealighterSession::buffering_thread()
{
    std::mutex thread_mutex;
    std::unique_lock<std::mutex> lock(thread_mutex);
    auto time_point = std::chrono::system_clock::now() +
        std::chrono::seconds(buffer_lists_timeout_seconds_);
    while (!buffer_thread_stop_) {
        while (buffer_list_con_var_.wait_until(lock, time_point) == std::cv_status::timeout) {
            flush_buffered_lists();
            time_point = std::chrono::system_clock::now() +
                std::chrono::seconds(buffer_lists_timeout_seconds_);
        }
    }

    // Flush one last time before ending
    flush_buffered_lists();
}

void SealighterSession::start_bufferring()
{
    // Only start buffer thread if we need to
    if (buffer_lists_.size() != 0 && !buffer_thread_stop_.load()) {
        buffer_list_thread_ = std::thread(&SealighterSession::buffering_thread, this);
    }
}


void SealighterSession::stop_bufferring()
{
    if (buffer_lists_.size() != 0 && !buffer_thread_stop_.load()) {
        buffer_thread_stop_ = true;
        buffer_list_con_var_.notify_one();
        buffer_list_thread_.join();
    }
}
