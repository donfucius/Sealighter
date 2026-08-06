#pragma once
#include "sealighter_krabs.h"
#include "sealighter_json.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <thread>


struct event_buffer_t {
    event_buffer_t()
    {}

    json json_event;
};


struct event_buffer_list_t {
    event_buffer_list_t
    (
        std::uint32_t id,
        std::uint32_t max
    )
        : event_id(id)
        , max_before_buffering(max)
        , event_count(0)
    {}

    const std::uint32_t event_id;
    const std::uint32_t max_before_buffering;

    std::uint32_t event_count;
    std::vector<std::string> properties_to_compare;
    std::vector<json> json_event_buffered;
};

struct sealighter_context_t {
    sealighter_context_t
    (
        std::string name,
        bool dump_event
    )
        : trace_name(name)
        , dump_raw_event(dump_event)
    {}

    const std::string trace_name;
    const bool dump_raw_event;
};

enum Output_format
{
    output_stdout,
    output_event_log,
    output_file
};

class SealighterSession {
public:
    SealighterSession() = default;
    ~SealighterSession() = default;

    SealighterSession(const SealighterSession&) = delete;
    SealighterSession& operator=(const SealighterSession&) = delete;

    int run(const std::string& config_string);
    void stop();

    void handle_event(
        const EVENT_RECORD& record,
        const krabs::trace_context& trace_context
    );

    void handle_event_context(
        const EVENT_RECORD& record,
        const krabs::trace_context& trace_context,
        std::shared_ptr<struct sealighter_context_t> event_context
    );

    int setup_logger_file(const std::string& filename);
    void teardown_logger_file();
    void set_output_format(Output_format format);

    void add_buffered_list(
        const std::string& trace_name,
        event_buffer_list_t buffered_list
    );
    void set_buffer_lists_timeout(uint32_t timeout);
    void start_bufferring();
    void stop_bufferring();

    int add_kernel_traces(const json& json_config, EVENT_TRACE_PROPERTIES session_properties);
    int add_user_traces(const json& json_config, EVENT_TRACE_PROPERTIES session_properties, const std::wstring& session_name);

    int parse_config(const std::string& config_string);

private:
    json parse_event_to_json(
        const EVENT_RECORD& record,
        const krabs::trace_context& ctx,
        std::shared_ptr<struct sealighter_context_t> sealighter_context,
        krabs::schema schema
    );
    void output_json_event(const json& json_event);
    void threaded_print_ln(const std::string& event_string);
    void write_event_log(const json& json_event, const std::string& trace_name, const std::string& event_string);
    void threaded_write_file_ln(const std::string& event_string);
    void flush_buffered_lists();
    void buffering_thread();

    void WaitForStopEvent();

    template <typename T>
    void run_trace(krabs::trace<T>* trace);

    template <typename ComparerA, typename ComparerW>
    void add_filter_to_vector_property_compare_item(
        const json& item,
        std::vector<std::shared_ptr<krabs::predicates::details::predicate_base>>& list
    );

    template <typename ComparerA, typename ComparerW>
    void add_filter_to_vector_property_compare(
        const json& root,
        const std::string& element,
        std::vector<std::shared_ptr<krabs::predicates::details::predicate_base>>& pred_vector
    );

    void add_filter_to_vector_property_is_item(
        const json& item,
        std::vector<std::shared_ptr<krabs::predicates::details::predicate_base>>& list
    );

    void add_filter_to_vector_property_is(
        const json& root,
        std::vector<std::shared_ptr<krabs::predicates::details::predicate_base>>& pred_vector
    );

    template <typename TPred, typename TJson1 = std::uint64_t, typename TJson2 = std::uint64_t>
    void add_filter_to_vector_basic_pair(
        const json& root,
        const std::string& element,
        const std::string& item1_name,
        const std::string& item2_name,
        std::vector<std::shared_ptr<krabs::predicates::details::predicate_base>>& pred_vector
    );

    template <typename TPred, typename TJson1 = std::uint64_t>
    void add_filter_to_vector_basic(
        const json& root,
        const std::string& element,
        std::vector<std::shared_ptr<krabs::predicates::details::predicate_base>>& pred_vector
    );

    int add_filters_to_vector(
        std::vector<std::shared_ptr<krabs::predicates::details::predicate_base>>& pred_vector,
        const json& json_list
    );

    template <typename T>
    int add_filters(
        krabs::details::base_provider<T>* pNew_provider,
        std::shared_ptr<struct sealighter_context_t> sealighter_context,
        const json& json_provider
    );

    std::ofstream outfile_;
    std::mutex print_mutex_;
    Output_format output_format_ = output_stdout;

    std::map<std::string, std::vector<event_buffer_list_t>> buffer_lists_;
    std::uint32_t buffer_lists_timeout_seconds_ = 5;
    std::mutex buffer_lists_mutex_;
    std::thread buffer_list_thread_;
    std::atomic_bool buffer_thread_stop_{ false };
    std::condition_variable buffer_list_con_var_;

    std::unique_ptr<krabs::user_trace> user_session_;
    std::unique_ptr<krabs::kernel_trace> kernel_session_;
    std::vector<std::unique_ptr<krabs::kernel_provider>> kernel_providers_;
    std::vector<std::unique_ptr<krabs::provider<>>> user_providers_;

    std::atomic<int> stop_event_error_{ 0 };
};
