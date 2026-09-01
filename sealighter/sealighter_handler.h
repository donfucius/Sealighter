#pragma once
#include "sealighter_krabs.h"
#include "sealighter_json.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>


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
    ~SealighterSession() {
        stop();                  // Ensure trace sessions are stopped
        stop_event_processing(); // Ensure worker thread is joined
    };

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

    void start_event_processing();
    void stop_event_processing();

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

    // -----------------------------------------------------------------
    // Decoupled event processing: raw event queue + worker thread
    // -----------------------------------------------------------------
    struct raw_event_t {
        EVENT_RECORD record;
        std::vector<BYTE> user_data;
        std::vector<BYTE> extended_payloads;
        std::vector<EVENT_HEADER_EXTENDED_DATA_ITEM> extended_items;
        std::shared_ptr<sealighter_context_t> context;
        const krabs::trace_context* trace_ctx;
    };

    class raw_event_queue_t {
    public:
        explicit raw_event_queue_t(size_t capacity)
            : capacity_(capacity), dropped_(0) {}

        bool enqueue(std::unique_ptr<raw_event_t> ev) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (ev == nullptr) {
                cv_.notify_one();
                return true;
            }
            if (queue_.size() >= capacity_) {
                ++dropped_;
                return false;
            }
            queue_.push_back(std::move(ev));
            cv_.notify_one();
            return true;
        }

        std::unique_ptr<raw_event_t> dequeue(
            const std::atomic_bool& stop,
            std::chrono::milliseconds timeout,
            bool& timed_out)
        {
            std::unique_lock<std::mutex> lock(mutex_);
            timed_out = false;
            auto deadline = std::chrono::steady_clock::now() + timeout;
            bool woke = cv_.wait_until(lock, deadline, [&] {
                return !queue_.empty() || stop.load();
            });
            if (queue_.empty()) {
                if (!woke) timed_out = true;
                return nullptr;
            }
            auto ev = std::move(queue_.front());
            queue_.pop_front();
            return ev;
        }

        std::unique_ptr<raw_event_t> try_dequeue() {
            std::lock_guard<std::mutex> lock(mutex_);
            if (queue_.empty()) return nullptr;
            auto ev = std::move(queue_.front());
            queue_.pop_front();
            return ev;
        }

        uint64_t dropped() const { return dropped_.load(); }

    private:
        size_t capacity_;
        std::deque<std::unique_ptr<raw_event_t>> queue_;
        mutable std::mutex mutex_;
        std::condition_variable cv_;
        std::atomic<uint64_t> dropped_;
    };

    void copy_event_record_(
        const EVENT_RECORD& record,
        const krabs::trace_context& trace_ctx,
        std::shared_ptr<struct sealighter_context_t> context,
        raw_event_t& out);

    void process_raw_event_(raw_event_t& raw);
    void event_worker_thread_();

    template <typename T>
    void query_trace_lost_(krabs::trace<T>* trace);
    void query_and_update_kernel_lost_();
    void log_drop_counts_(bool final = false);

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
    std::atomic_bool stopped_{ false };  // Idempotent guard for stop()

    raw_event_queue_t event_queue_{ 8192 };
    std::thread event_worker_thread_handle_;
    std::atomic_bool event_worker_stop_flag_{ false };
    std::atomic<std::uint64_t> kernel_events_lost_{ 0 };
    static constexpr std::chrono::milliseconds event_worker_log_interval_ =
        std::chrono::milliseconds(5000);
};
