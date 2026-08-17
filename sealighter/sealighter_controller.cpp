#include "sealighter_krabs.h"
#include <array>
#include <iostream>
#include <fstream>
#include <thread>
#include <memory>
#include <atomic>
#include "sealighter_errors.h"
#include "sealighter_util.h"
#include "sealighter_json.h"
#include "sealighter_predicates.h"
#include "sealighter_handler.h"
#include "sealighter_provider.h"
#include "sync.h"

#include "logger.h"
#define loggr (logger::Logger::GetInstance().logger())

// -------------------------
// FILE-LOCAL CONSTANTS
// -------------------------

// Add aliases to make code cleaner
namespace kpc = krabs::predicates::comparers;
namespace kpa = krabs::predicates::adapters;

// Sealighter events
constexpr std::wstring_view EVENT_SEALIGHTER_STARTED{ LR"(Local\SealighterStarted)" };
constexpr std::wstring_view EVENT_STOP_SEALIGHTER{ LR"(Local\StopSealighter)" };

// File-local pointer for Ctrl+C handler to access the active session
static SealighterSession* g_active_session = nullptr;

// -------------------------
// FILE-LOCAL STATIC FUNCTIONS
// -------------------------

static inline void SetSealighterStartedEvent()
{
	// notify the process that is waiting for the trace to start
	winxx::NamedEvent<wchar_t> sealighterEvent{ EVENT_SEALIGHTER_STARTED.data(), EVENT_MODIFY_STATE, FALSE };
	sealighterEvent.Set();
}

/*
    Handler for Ctrl+C cancel events.
    Makes sure we stop our ETW Session when shutting down
*/
static BOOL WINAPI crl_c_handler(DWORD fdwCtrlType)
{
    switch (fdwCtrlType)
    {
    case CTRL_C_EVENT:
        if (g_active_session) {
            g_active_session->stop();
        }
        return TRUE;
    }
    return FALSE;
}

// -------------------------
// MEMBER FUNCTIONS - START
// -------------------------

/*
    Adds a single property comparer filter to a list
*/
template <
    typename ComparerA,
    typename ComparerW
>
void SealighterSession::add_filter_to_vector_property_compare_item
(
    const json& item,
    std::vector<std::shared_ptr<krabs::predicates::details::predicate_base>>& list
)
{
    if (!(!item.contains("name") || item["name"].is_null()) && !(!item.contains("value") || item["value"].is_null()) && !(!item.contains("type") || item["type"].is_null())) {
        std::wstring name = convert_str_wstr(item["name"].get<std::string>());
        std::string type = item["type"].get<std::string>();
        if (type == "STRINGA") {
            std::string val = item["value"].get<std::string>();
            auto pred = std::make_shared<
                krabs::predicates::details::property_view_predicate<
                std::string,
                kpa::generic_string<char>,
                ComparerA
                >>(
                name,
                val,
                kpa::generic_string<char>(),
                ComparerA()
                );
            list.emplace_back(pred);
        }
        else if (type == "STRINGW") {
            std::wstring val = convert_str_wstr(item["value"].get<std::string>());

            auto pred = std::make_shared<
                krabs::predicates::details::property_view_predicate<
                std::wstring,
                kpa::generic_string<wchar_t>,
                ComparerW
                >>(
                name,
                val,
                kpa::generic_string<wchar_t>(),
                ComparerW()
                );
            list.emplace_back(pred);
        }
        else {
            // Raise a parse error, type has to be a string
            throw nlohmann::detail::exception(
                nlohmann::detail::parse_error::create(0, 0,
                    "The 'type' of a Property Comparer must be 'STRINGA' or 'STRINGW'"));
        }
    }
    else {
        // Raise a parse error, properites *must* have all these fields
        throw nlohmann::detail::exception(
            nlohmann::detail::parse_error::create(
                0, 0, "Properties must have a 'name', 'type' AND 'value' keys "));
    }
}


/*
    Adds a property comparer filter to a list
*/
template <
    typename ComparerA,
    typename ComparerW
>
void SealighterSession::add_filter_to_vector_property_compare
(
    const json& root,
    const std::string& element,
    std::vector<std::shared_ptr<krabs::predicates::details::predicate_base>>& pred_vector
)
{
    std::vector<std::shared_ptr<krabs::predicates::details::predicate_base>> list;
    if (!(!root.contains(element) || root[element].is_null())) {
        loggr.info("        {}: {}", element.c_str(), convert_json_string(root[element], false).c_str());
        if ((root.contains(element) && root[element].is_array())) {
            for (json item : root[element]) {
                this->add_filter_to_vector_property_compare_item<ComparerA, ComparerW>(item, list);
            }
            if (!list.empty()) {
                pred_vector.emplace_back(std::make_shared<sealighter_any_of>(list));
            }
        }
        else {
            this->add_filter_to_vector_property_compare_item<ComparerA, ComparerW>(root[element], pred_vector);
        }
    }
}


/*
    Add a single "property is" filter to a list.
*/
void SealighterSession::add_filter_to_vector_property_is_item
(
    const json& item,
    std::vector<std::shared_ptr<krabs::predicates::details::predicate_base>>& list
)
{
    if (!(!item.contains("name") || item["name"].is_null()) && !(!item.contains("value") || item["value"].is_null()) && !(!item.contains("type") || item["type"].is_null())) {
        std::wstring name = convert_str_wstr(item["name"].get<std::string>());
        std::string type = item["type"].get<std::string>();
        if (type == "STRINGA") {
            auto val = item["value"].get<std::string>();
            list.emplace_back(std::make_shared<sealighter_property_is<std::string>>
                (name, val));
        }
        else if (type == "STRINGW") {
            auto val = convert_str_wstr(item["value"].get<std::string>());
            list.emplace_back(std::make_shared<sealighter_property_is<std::wstring>>
                (name, val));
        }
        else if (type == "INT8") {
            auto val = item["value"].get<std::int8_t>();
            list.emplace_back(std::make_shared<sealighter_property_is<std::int8_t>>
                (name, val));
        }
        else if (type == "UINT8") {
            auto val = item["value"].get<std::uint8_t>();
            list.emplace_back(std::make_shared<sealighter_property_is<std::uint8_t>>
                (name, val));
        }
        else if (type == "INT16") {
            auto val = item["value"].get<std::int16_t>();
            list.emplace_back(std::make_shared<sealighter_property_is<std::int16_t>>
                (name, val));
        }
        else if (type == "UINT16") {
            auto val = item["value"].get<std::uint16_t>();
            list.emplace_back(std::make_shared<sealighter_property_is<std::uint16_t>>
                (name, val));
        }
        else if (type == "INT32") {
            auto val = item["value"].get<std::int32_t>();
            list.emplace_back(std::make_shared<sealighter_property_is<std::int32_t>>
                (name, val));
        }
        else if (type == "UINT32") {
            auto val = item["value"].get<std::uint32_t>();
            list.emplace_back(std::make_shared<sealighter_property_is<std::uint32_t>>
                (name, val));
        }
        else if (type == "INT64") {
            auto val = item["value"].get<std::int64_t>();
            list.emplace_back(std::make_shared<sealighter_property_is<std::int64_t>>
                (name, val));
        }
        else if (type == "UINT64") {
            auto val = item["value"].get<std::uint64_t>();
            list.emplace_back(std::make_shared<sealighter_property_is<std::uint64_t>>
                (name, val));
        }
        else if (type == "GUID") {
            const auto val = convert_str_guid(item["value"].get<std::string>());
            list.emplace_back(std::make_shared<sealighter_property_is<krabs::guid>>
                (name, val));
        }
    }
    else {
        // Raise an parse error, properites *must* have all these fields
        throw nlohmann::detail::exception(
            nlohmann::detail::parse_error::create
            (0, 0, "Properties must have a 'name', 'type' AND 'value' keys "));
    }
}


/*
    Add the "Property Is" filter to a list,
    if availible. This is a different type of predicate
    to both the basic predicates and the other property comparer ones
*/
void SealighterSession::add_filter_to_vector_property_is
(
    const json& root,
    std::vector<std::shared_ptr<krabs::predicates::details::predicate_base>>& pred_vector
)
{
    std::vector<std::shared_ptr<krabs::predicates::details::predicate_base>> list;
    if (!root.is_null()) {
        loggr.info("        Property Is: {}", convert_json_string(root, false).c_str());
        if (root.is_array()) {
            for (json item : root) {
                this->add_filter_to_vector_property_is_item(item, list);
            }
            if (!list.empty()) {
                pred_vector.emplace_back(std::make_shared<sealighter_any_of>(list));
            }
        }
        else {
            this->add_filter_to_vector_property_is_item(root, pred_vector);
        }
    }
};

/*
    Adds a basic filter with two values
*/
template <
    typename TPred,
    typename TJson1,
    typename TJson2
>
void SealighterSession::add_filter_to_vector_basic_pair
(
    const json& root,
    const std::string& element,
    const std::string& item1_name,
    const std::string& item2_name,
    std::vector<std::shared_ptr<krabs::predicates::details::predicate_base>>& pred_vector
)
{
    std::vector<std::shared_ptr<krabs::predicates::details::predicate_base>> list;
    if (!(!root.contains(element) || root[element].is_null())) {
        loggr.info("        {}: {}", element.c_str(), convert_json_string(root[element], false).c_str());
        if ((root.contains(element) && root[element].is_array())) {
            for (json item : root[element]) {
                if (!(!item.contains(item1_name) || item[item1_name].is_null()) && !(!item.contains(item2_name) || item[item2_name].is_null())) {
                    TJson1 item1 = item[item1_name].get<TJson1>();
                    TJson2 item2 = item[item2_name].get<TJson2>();
                    list.emplace_back(std::make_shared<TPred>(item1, item2));
                }
            }
            if (!list.empty()) {
                pred_vector.emplace_back(std::make_shared<sealighter_any_of>(list));
            }
        }
        else {
            if (root[element].contains(item1_name) && !root[element][item1_name].is_null() &&
                root[element].contains(item2_name) && !root[element][item2_name].is_null()) {
                TJson1 item1 = root[element][item1_name].get<TJson1>();
                TJson2 item2 = root[element][item2_name].get<TJson2>();
                pred_vector.emplace_back(std::make_shared<TPred>(item1, item2));
            }
        }
    }
}


/*
    Parse JSON to add filters to a vector list
    JSON can be a single item, or an array of items that we will 'OR'
*/
template <
    typename TPred,
    typename TJson1
>
void SealighterSession::add_filter_to_vector_basic
(
    const json& root,
    const std::string& element,
    std::vector<std::shared_ptr<krabs::predicates::details::predicate_base>>& pred_vector
)
{
    std::vector<std::shared_ptr<krabs::predicates::details::predicate_base>> list;
    if (!(!root.contains(element) || root[element].is_null())) {
        loggr.info("        {}: {}", element.c_str(), convert_json_string(root[element], false).c_str());
        // If a list, filter can be any of them
        if ((root.contains(element) && root[element].is_array())) {
            for (json item : root[element]) {
                list.emplace_back(std::make_shared<TPred>(item.get<TJson1>()));
            }
            if (!list.empty()) {
                pred_vector.emplace_back(std::make_shared<sealighter_any_of>(list));
            }
        }
        else {
            pred_vector.emplace_back(std::make_shared<TPred>
                (root[element].get<TJson1>()));
        }
    }
}


/*
    Parse JSON to add filters to a vector list
*/
int SealighterSession::add_filters_to_vector
(
    std::vector<std::shared_ptr<krabs::predicates::details::predicate_base>>& pred_vector,
    const json& json_list
)
{
    int status = ERROR_SUCCESS;
    try {
        // Add the basic single-value filters
        this->add_filter_to_vector_basic<krabs::predicates::id_is>
            (json_list, "event_id_is", pred_vector);
        this->add_filter_to_vector_basic<krabs::predicates::opcode_is>
            (json_list, "opcode_is", pred_vector);
        this->add_filter_to_vector_basic<krabs::predicates::process_id_is>
            (json_list, "process_id_is", pred_vector);
        this->add_filter_to_vector_basic<krabs::predicates::version_is>
            (json_list, "version_is", pred_vector);

        // Add all the property filters
        this->add_filter_to_vector_property_is(json_list.value("property_is", json::array()), pred_vector);

        this->add_filter_to_vector_property_compare<
            kpc::equals<std::equal_to<kpa::generic_string<char>::value_type>>,
            kpc::equals<std::equal_to<kpa::generic_string<wchar_t>::value_type>>
        >(json_list, "property_equals", pred_vector);

        this->add_filter_to_vector_property_compare<
            kpc::equals<kpc::iequal_to<kpa::generic_string<char>::value_type>>,
            kpc::equals<kpc::iequal_to<kpa::generic_string<wchar_t>::value_type>>
        >(json_list, "property_iequals", pred_vector);

        this->add_filter_to_vector_property_compare<
            kpc::contains<std::equal_to<kpa::generic_string<char>::value_type>>,
            kpc::contains<std::equal_to<kpa::generic_string<wchar_t>::value_type>>
        >(json_list, "property_contains", pred_vector);

        this->add_filter_to_vector_property_compare<
            kpc::contains<kpc::iequal_to<kpa::generic_string<char>::value_type>>,
            kpc::contains<kpc::iequal_to<kpa::generic_string<wchar_t>::value_type>>
        >(json_list, "property_icontains", pred_vector);

        this->add_filter_to_vector_property_compare<
            kpc::starts_with<std::equal_to<kpa::generic_string<char>::value_type>>,
            kpc::starts_with<std::equal_to<kpa::generic_string<wchar_t>::value_type>>
        >(json_list, "property_starts_with", pred_vector);

        this->add_filter_to_vector_property_compare<
            kpc::starts_with<kpc::iequal_to<kpa::generic_string<char>::value_type>>,
            kpc::starts_with<kpc::iequal_to<kpa::generic_string<wchar_t>::value_type>>
        >(json_list, "property_istarts_with", pred_vector);

        this->add_filter_to_vector_property_compare<
            kpc::ends_with<std::equal_to<kpa::generic_string<char>::value_type>>,
            kpc::ends_with<std::equal_to<kpa::generic_string<wchar_t>::value_type>>
        >(json_list, "property_ends_with", pred_vector);

        this->add_filter_to_vector_property_compare<
            kpc::ends_with<kpc::iequal_to<kpa::generic_string<char>::value_type>>,
            kpc::ends_with<kpc::iequal_to<kpa::generic_string<wchar_t>::value_type>>
        >(json_list, "property_iends_with", pred_vector);

        // Add own own created Predicates
        this->add_filter_to_vector_basic<sealighter_max_events_total, std::uint64_t>
            (json_list, "max_events_total", pred_vector);
        this->add_filter_to_vector_basic_pair<sealighter_max_events_id>
            (json_list, "max_events_id", "id_is", "max_events", pred_vector);
        this->add_filter_to_vector_basic<sealighter_any_field_contains, std::string>
            (json_list, "any_field_contains", pred_vector);
        this->add_filter_to_vector_basic<sealighter_process_name_contains, std::string>
            (json_list, "process_name_contains", pred_vector);
        this->add_filter_to_vector_basic<sealighter_activity_id_is, std::string>
            (json_list, "activity_id_is", pred_vector);
    }
    catch (const nlohmann::detail::exception& e) {
        loggr.info("failed to add filters from config to provider");
        loggr.info("{}", e.what());
        status = SEALIGHTER_ERROR_PARSE_FILTER;
    }
    return status;
}


/*
    Add Krabs filters to an ETW provider
*/
template <typename T>
int SealighterSession::add_filters
(
    krabs::details::base_provider<T>* pNew_provider,
    std::shared_ptr<struct sealighter_context_t> sealighter_context,
    const json& json_provider
)
{
    int status = ERROR_SUCCESS;

    if ((!json_provider.contains("filters") || json_provider["filters"].is_null()) ||
        (!json_provider["filters"].contains("any_of") &&
         !json_provider["filters"].contains("all_of") &&
         !json_provider["filters"].contains("none_of"))
       ) {
        // No filters, log everything
        loggr.info("    No event filters");
        pNew_provider->add_on_event_callback([this, sealighter_context](const EVENT_RECORD& record, const krabs::trace_context& trace_context) {
            this->handle_event_context(record, trace_context, sealighter_context);
        });
    }
    else {
        // Build top-level list
        // All 3 options will eventually be ANDed together
        std::vector<std::shared_ptr<krabs::predicates::details::predicate_base>> top_list;
        if (json_provider["filters"].contains("any_of") && !json_provider["filters"]["any_of"].is_null()) {
            loggr.info("    Filtering any of:");
            std::vector<std::shared_ptr<krabs::predicates::details::predicate_base>> list;
            status = this->add_filters_to_vector(list, json_provider["filters"]["any_of"]);
            if (ERROR_SUCCESS == status) {
                top_list.emplace_back(std::make_shared<sealighter_any_of>(list));
            }
        }
        if (ERROR_SUCCESS == status && json_provider["filters"].contains("all_of") && !json_provider["filters"]["all_of"].is_null()) {
            loggr.info("    Filtering all of:");
            std::vector<std::shared_ptr<krabs::predicates::details::predicate_base>> list;
            status = this->add_filters_to_vector(list, json_provider["filters"]["all_of"]);
            if (ERROR_SUCCESS == status) {
                top_list.emplace_back(std::make_shared<sealighter_all_of>(list));
            }
        }
        if (ERROR_SUCCESS == status && json_provider["filters"].contains("none_of") && !json_provider["filters"]["none_of"].is_null()) {
            loggr.info("    Filtering none of:");
            std::vector<std::shared_ptr<krabs::predicates::details::predicate_base>> list;
            status = this->add_filters_to_vector(list, json_provider["filters"]["none_of"]);
            if (ERROR_SUCCESS == status) {
                top_list.emplace_back(std::make_shared<sealighter_none_of>(list));
            }
        }

        // Add top level list to a filter
        if (ERROR_SUCCESS == status) {
            sealighter_all_of top_pred = sealighter_all_of(top_list);
            krabs::event_filter filter(top_pred);

            filter.add_on_event_callback([this, sealighter_context](const EVENT_RECORD& record, const krabs::trace_context& trace_context) {
                this->handle_event_context(record, trace_context, sealighter_context);
            });
            pNew_provider->add_filter(filter);
        }
    }

    return status;
}


/*
    Add Kernel Providers and Create Kernel ETW Session
*/
int SealighterSession::add_kernel_traces
(
    const json& json_config,
    EVENT_TRACE_PROPERTIES  session_properties
)
{
    int status = ERROR_SUCCESS;
    std::string provider_name;

    // Initialize Session and props
    kernel_session_ = std::make_unique<krabs::kernel_trace>();
    kernel_session_->set_trace_properties(&session_properties);

    // Add any Kernel providers
    try {
        if (json_config.contains("kernel_traces"))
        for (json json_provider : json_config["kernel_traces"]) {
            if ((!json_provider.contains("provider_name") || json_provider["provider_name"].is_null())) {
                loggr.info("Invalid Provider, missing provider name");
                status = SEALIGHTER_ERROR_PARSE_KERNEL_PROVIDER;
                break;
            }
            provider_name = json_provider["provider_name"].get<std::string>();

            std::unique_ptr<krabs::kernel_provider> pNew_provider;

            static const auto kernel_provider_factories = [] {
                using factory_t = std::function<std::unique_ptr<krabs::kernel_provider>()>;
                return std::map<std::string, factory_t>{
                    { "process",            [] { return std::make_unique<krabs::kernel::process_provider>(); } },
                    { "thread",             [] { return std::make_unique<krabs::kernel::thread_provider>(); } },
                    { "image_load",         [] { return std::make_unique<krabs::kernel::image_load_provider>(); } },
                    { "process_counter",    [] { return std::make_unique<krabs::kernel::process_counter_provider>(); } },
                    { "context_switch",     [] { return std::make_unique<krabs::kernel::context_switch_provider>(); } },
                    { "dpc",                [] { return std::make_unique<krabs::kernel::dpc_provider>(); } },
                    { "debug_print",        [] { return std::make_unique<krabs::kernel::debug_print_provider>(); } },
                    { "interrupt",          [] { return std::make_unique<krabs::kernel::interrupt_provider>(); } },
                    { "system_call",        [] { return std::make_unique<krabs::kernel::system_call_provider>(); } },
                    { "disk_io",            [] { return std::make_unique<krabs::kernel::disk_io_provider>(); } },
                    { "disk_file_io",       [] { return std::make_unique<krabs::kernel::disk_file_io_provider>(); } },
                    { "disk_init_io",       [] { return std::make_unique<krabs::kernel::disk_init_io_provider>(); } },
                    { "thread_dispatch",    [] { return std::make_unique<krabs::kernel::thread_dispatch_provider>(); } },
                    { "memory_page_fault",  [] { return std::make_unique<krabs::kernel::memory_page_fault_provider>(); } },
                    { "memory_hard_fault",  [] { return std::make_unique<krabs::kernel::memory_hard_fault_provider>(); } },
                    { "virtual_alloc",      [] { return std::make_unique<krabs::kernel::virtual_alloc_provider>(); } },
                    { "network_tcpip",      [] { return std::make_unique<krabs::kernel::network_tcpip_provider>(); } },
                    { "registry",           [] { return std::make_unique<krabs::kernel::registry_provider>(); } },
                    { "alpc",               [] { return std::make_unique<krabs::kernel::alpc_provider>(); } },
                    { "split_io",           [] { return std::make_unique<krabs::kernel::split_io_provider>(); } },
                    { "driver",             [] { return std::make_unique<krabs::kernel::driver_provider>(); } },
                    { "profile",            [] { return std::make_unique<krabs::kernel::profile_provider>(); } },
                    { "file_io",            [] { return std::make_unique<krabs::kernel::file_io_provider>(); } },
                    { "file_init_io",       [] { return std::make_unique<krabs::kernel::file_init_io_provider>(); } },
                    { "vamap",              [] { return std::make_unique<krabs::kernel::vamap_provider>(); } },
                    { "object_manager",     [] { return std::make_unique<krabs::kernel::object_manager_provider>(); } },
                };
            }();

            auto it = kernel_provider_factories.find(provider_name);
            if (it != kernel_provider_factories.end()) {
                pNew_provider = it->second();
            } else {
                loggr.info("Invalid Provider: {}", provider_name.c_str());
                status = SEALIGHTER_ERROR_PARSE_KERNEL_PROVIDER;
                break;
            }

            // Create context with trace name
            if ((!json_provider.contains("trace_name") || json_provider["trace_name"].is_null())) {
                loggr.info("Invalid Provider, missing trace name");
                status = SEALIGHTER_ERROR_PARSE_KERNEL_PROVIDER;
                break;
            }

            std::string trace_name = json_provider["trace_name"].get<std::string>();
            auto sealighter_context =
                std::make_shared<sealighter_context_t>(trace_name, false);
            if (json_provider.contains("buffers"))
        for (json json_buffers : json_provider["buffers"]) {
                auto event_id = json_buffers["event_id"].get<std::uint32_t>();
                auto max = json_buffers["max_before_buffering"].get<std::uint32_t>();
                auto buffer_list = event_buffer_list_t(event_id, max);
                for (json json_buff_prop: json_buffers["properties_to_match"]) {
                    buffer_list.properties_to_compare.push_back(json_buff_prop.get<std::string>());
                }

                this->add_buffered_list(trace_name, buffer_list);
            }


            // Add any filters
            loggr.info("Kernel Provider: {}", provider_name.c_str());
            status = this->add_filters(pNew_provider.get(), sealighter_context, json_provider);
            if (ERROR_SUCCESS == status) {
                kernel_providers_.push_back(std::move(pNew_provider));
                kernel_session_->enable(*kernel_providers_.back());
            }
            else {
                loggr.info("Failed to add filters to: {}", provider_name.c_str());
                break;
            }
        }
    }
    catch (const nlohmann::detail::exception & e) {
        loggr.info("invalid kernel provider in config file");
        loggr.info("{}", e.what());
        status = SEALIGHTER_ERROR_PARSE_KERNEL_PROVIDER;
    }
    return status;
}

/*
    Add User providers and create User ETW Session
*/
int SealighterSession::add_user_traces
(
    const json& json_config,
    EVENT_TRACE_PROPERTIES  session_properties,
    const std::wstring& session_name
)
{
    int status = ERROR_SUCCESS;
    // Initialize Session and props
    user_session_ = std::make_unique<krabs::user_trace>(session_name);
    user_session_->set_trace_properties(&session_properties);
    try {
        // Parse the Usermode Providers
        if (json_config.contains("user_traces"))
        for (json json_provider : json_config["user_traces"]) {
            GUID provider_guid;
            std::unique_ptr<krabs::provider<>> pNew_provider;
            std::wstring provider_name;
            std::string trace_name;
            if ((!json_provider.contains("provider_name") || json_provider["provider_name"].is_null())) {
                loggr.info("Invalid Provider");
                status = SEALIGHTER_ERROR_PARSE_USER_PROVIDER;
                break;
            }

            if ((!json_provider.contains("trace_name") || json_provider["trace_name"].is_null())) {
                loggr.info("Invalid Provider, missing trace name");
                status = SEALIGHTER_ERROR_PARSE_KERNEL_PROVIDER;
                break;
            }
            trace_name = json_provider["trace_name"].get<std::string>();

            // If provider_name is a GUID, use that
            // Otherwise pass it off to Krabs to try to resolve
            provider_name = convert_str_wstr(json_provider["provider_name"].get<std::string>());
            provider_guid = convert_wstr_guid(provider_name);

            if (provider_guid != GUID_NULL) {
                pNew_provider = std::make_unique<krabs::provider<>>(provider_guid);
            }
            else {
                try {
                    pNew_provider = std::make_unique<krabs::provider<>>(provider_name);
                }
                catch (const std::exception & e) {
                    loggr.info("{}", e.what());
                    status = SEALIGHTER_ERROR_NO_PROVIDER;
                    break;
                }
            }
            loggr.info("User Provider: {}", convert_wstr_str(provider_name));
            loggr.info("    Trace Name: {}", trace_name.c_str());

            // If no keywords_all or keywords_any is set
            // then set a default 'match anything'
            if ((!json_provider.contains("keywords_all") || json_provider["keywords_all"].is_null()) && (!json_provider.contains("keywords_any") || json_provider["keywords_any"].is_null())) {
                loggr.info("    Keywords: All");
            }
            else {
                if (!(!json_provider.contains("keywords_all") || json_provider["keywords_all"].is_null())) {
                    uint64_t data = json_provider["keywords_all"].get<std::uint64_t>();
                    loggr.info("    Keywords All: 0x{:x}", data);
                    pNew_provider->all(data);
                }

                if (!(!json_provider.contains("keywords_any") || json_provider["keywords_any"].is_null())) {
                    uint64_t data = json_provider["keywords_any"].get<std::uint64_t>();
                    loggr.info("    Keywords Any: 0x{:x}", data);
                    pNew_provider->any(data);
                }
            }
            if (!(!json_provider.contains("level") || json_provider["level"].is_null())) {
                uint64_t data = json_provider["level"].get<std::uint64_t>();
                loggr.info("    Level: 0x{:x}", data);
                pNew_provider->level(data);
            }
            else {
                // Set Max Level
                pNew_provider->level(0xff);
            }

            if (!(!json_provider.contains("trace_flags") || json_provider["trace_flags"].is_null())) {
                uint64_t data = json_provider["trace_flags"].get<std::uint64_t>();
                loggr.info("    Trace Flags: 0x{:x}", data);
                pNew_provider->trace_flags(data);
            }

            // Check if we want a stacktrace
            // This is just a helper option, you could also set this in the trace_flags
            if (!(!json_provider.contains("report_stacktrace") || json_provider["report_stacktrace"].is_null()) && json_provider["report_stacktrace"].get<bool>()) {
                // Add the stacktrace trace flag
                pNew_provider->trace_flags(pNew_provider->trace_flags() | EVENT_ENABLE_PROPERTY_STACK_TRACE);
            }

            // Create context with trace name

            // Check if we're dumping the raw event, or attempting to parse it
            bool dump_raw_event = false;
            if (!(!json_provider.contains("dump_raw_event") || json_provider["dump_raw_event"].is_null())) {
                dump_raw_event = json_provider["dump_raw_event"].get<bool>();
                if (dump_raw_event) {
                    loggr.info("    Recording raw events");
                }
            }

            auto sealighter_context =
                std::make_shared<sealighter_context_t>(trace_name, dump_raw_event);
            if (json_provider.contains("buffers"))
        for (json json_buffers : json_provider["buffers"]) {
                auto event_id = json_buffers["event_id"].get<std::uint32_t>();
                auto max = json_buffers["max_before_buffering"].get<std::uint32_t>();
                auto buffer_list = event_buffer_list_t(event_id, max);
                if (json_buffers.contains("properties_to_match"))
        for (json json_buff_prop : json_buffers["properties_to_match"]) {
                    buffer_list.properties_to_compare.push_back(json_buff_prop.get<std::string>());
                }

                this->add_buffered_list(trace_name, buffer_list);
            }

            // Add any filters
            status = this->add_filters(pNew_provider.get(), sealighter_context, json_provider);
            if (ERROR_SUCCESS == status) {
                user_providers_.push_back(std::move(pNew_provider));
                user_session_->enable(*user_providers_.back());
            }
            else {
                loggr.info("Failed to add filters to: {}", convert_wstr_str(provider_name));
                break;
            }
        }
    }
    catch (const nlohmann::detail::exception & e) {
        loggr.info("invalid providers in config file");
        loggr.info("{}", e.what());
        status = SEALIGHTER_ERROR_PARSE_USER_PROVIDER;
    }

    // If everything is good, also add a default handler
    if (ERROR_SUCCESS == status) {
        user_session_->set_default_event_callback([](const EVENT_RECORD& record, const krabs::trace_context& ctx) {
            g_active_session->handle_event(record, ctx);
        });
    }

    return status;
}


/*
    Parse the Config file, setup
    ETW Session and Krabs filters
*/
int SealighterSession::parse_config(const std::string& config_string)
{
    int     status = ERROR_SUCCESS;
    EVENT_TRACE_PROPERTIES  session_properties = { 0 };
    std::wstring    session_name = L"Sealighter-Trace";
    json    json_config;

    try {
        // Read in config file
        json_config = json::parse(config_string);

        // Set defaults
        session_properties.BufferSize = 256;
        session_properties.MinimumBuffers = 12;
        session_properties.MaximumBuffers = 48;
        session_properties.FlushTimer = 1;
        session_properties.LogFileMode = EVENT_TRACE_REAL_TIME_MODE | EVENT_TRACE_INDEPENDENT_SESSION_MODE;

        // Parse the config json for any custom properties
        json json_props = json_config.contains("session_properties") ? json_config["session_properties"] : json::object();
        if (!json_props.is_null())
        {
            if (!(!json_props.contains("session_name") || json_props["session_name"].is_null())) {
                session_name = convert_str_wstr(json_props["session_name"].get<std::string>());
                loggr.info("Session Name: {}", convert_wstr_str(session_name));
            }

            if (!(!json_props.contains("buffer_size") || json_props["buffer_size"].is_null())) {
                session_properties.BufferSize = json_props["buffer_size"].get<std::uint32_t>();
                if (session_properties.BufferSize < 1 || session_properties.BufferSize > 1024) {
                    loggr.info("buffer_size must be between 1 and 1024 KB");
                    status = SEALIGHTER_ERROR_PARSE_CONFIG_PROPS;
                }
            }

            if (ERROR_SUCCESS == status && !(!json_props.contains("minimum_buffers") || json_props["minimum_buffers"].is_null())) {
                session_properties.MinimumBuffers =
                    json_props["minimum_buffers"].get<std::uint32_t>();
                if (session_properties.MinimumBuffers < 2 || session_properties.MinimumBuffers > 10000) {
                    loggr.info("minimum_buffers must be between 2 and 10000");
                    status = SEALIGHTER_ERROR_PARSE_CONFIG_PROPS;
                }
            }

            if (ERROR_SUCCESS == status && !(!json_props.contains("maximum_buffers") || json_props["maximum_buffers"].is_null())) {
                session_properties.MaximumBuffers =
                    json_props["maximum_buffers"].get<std::uint32_t>();
                if (session_properties.MaximumBuffers < session_properties.MinimumBuffers ||
                    session_properties.MaximumBuffers > 100000) {
                    loggr.info("maximum_buffers must be >= minimum_buffers and <= 100000");
                    status = SEALIGHTER_ERROR_PARSE_CONFIG_PROPS;
                }
            }

            if (ERROR_SUCCESS == status && !(!json_props.contains("flush_timer") || json_props["flush_timer"].is_null())) {
                session_properties.FlushTimer =
                    json_props["flush_timer"].get<std::uint32_t>();
                if (session_properties.FlushTimer < 1 || session_properties.FlushTimer > 3600) {
                    loggr.info("flush_timer must be between 1 and 3600 seconds");
                    status = SEALIGHTER_ERROR_PARSE_CONFIG_PROPS;
                }
            }

            if (!(!json_props.contains("output_format") || json_props["output_format"].is_null())) {
                std::string format = json_props["output_format"].get<std::string>();
                if ("stdout" == format) {
                    this->set_output_format(Output_format::output_stdout);
                }
                else if ("event_log" == format) {
                    this->set_output_format(Output_format::output_event_log);
                }
                else if ("file" == format) {
                    if ((!json_props.contains("output_filename") || json_props["output_filename"].is_null())) {
                        loggr.info("When output_format == 'file', also set 'output_filename'");
                        status = SEALIGHTER_ERROR_OUTPUT_FILE;
                    }
                    else {
                        this->set_output_format(Output_format::output_file);
                        status = this->setup_logger_file(json_props["output_filename"].get<std::string>());
                    }
                }
                else {
                    loggr.info("Invalid output_format");
                    status = SEALIGHTER_ERROR_OUTPUT_FORMAT;
                }
                loggr.info("Outputs: {}", format.c_str());
            }

            if (!(!json_props.contains("buffering_timeout_seconds") || json_props["buffering_timeout_seconds"].is_null())) {
                auto timeout = json_props["buffering_timeout_seconds"].get<std::uint32_t>();
                this->set_buffer_lists_timeout(timeout);
            }
            else if (!(!json_props.contains("buffering_timout_seconds") || json_props["buffering_timout_seconds"].is_null())) {
                loggr.info("Warning: 'buffering_timout_seconds' is deprecated, use 'buffering_timeout_seconds'");
                auto timeout = json_props["buffering_timout_seconds"].get<std::uint32_t>();
                this->set_buffer_lists_timeout(timeout);
            }
        }
    }
    catch (const nlohmann::detail::exception& e) {
        loggr.info("invalid session properties in config file");
        loggr.info("{}", e.what());
        status = SEALIGHTER_ERROR_PARSE_CONFIG_PROPS;
    }

    if (ERROR_SUCCESS == status) {
        if ((!json_config.contains("user_traces") || json_config["user_traces"].is_null()) && (!json_config.contains("kernel_traces") || json_config["kernel_traces"].is_null())) {
            loggr.info("No User or Kernel providers in config file");
            status = SEALIGHTER_ERROR_PARSE_NO_PROVIDERS;
        }
        else {
            if (!(!json_config.contains("user_traces") || json_config["user_traces"].is_null())) {
                status = this->add_user_traces(json_config, session_properties, session_name);
            }

            // Add kernel providers if needed
            if (ERROR_SUCCESS == status && !(!json_config.contains("kernel_traces") || json_config["kernel_traces"].is_null())) {
                status = this->add_kernel_traces(json_config, session_properties);
            }
        }
    }

    return status;
}


/*
    Run a trace, and ensure we stop if something goes wrong
*/
template <typename T>
void SealighterSession::run_trace(krabs::trace<T>* trace)
{
    if (NULL != trace) {
        // Ensure we always stop the trace afterwards
        try {
            trace->start();
        }
        catch (const std::exception & e) {
            loggr.info("{}", e.what());
            trace->stop();
            throw;
        }
        catch (...) {
            trace->stop();
            throw;
        }
    }
}


/*
    Stop any running trace.
    Idempotent: safe to call multiple times (from stop event, Ctrl+C, destructor).
*/
void SealighterSession::stop()
{
    if (stopped_.exchange(true)) {
        return;  // Already stopped
    }
    if (user_session_) {
        user_session_->stop();
    }
    if (kernel_session_) {
        kernel_session_->stop();
    }
}


void SealighterSession::WaitForStopEvent()
{
    winxx::NamedEvent<wchar_t> evtStop{ EVENT_STOP_SEALIGHTER.data(), nullptr, TRUE, FALSE };
    if (evtStop.Wait(INFINITE) == WAIT_FAILED) {
        stop_event_error_ = SEALIGHTER_ERROR_WAIT_STOP;
        loggr.info("Waiting for the StopSealighter event failed.");
    } else {
        loggr.info("StopSealighter event received, stopping traces...");
    }
    try {
        this->stop();
    }
    catch (const std::exception& e) {
        loggr.info("Error stopping trace: {}", e.what());
    }
}

// -------------------------
// MEMBER FUNCTIONS - END
// -------------------------
// PUBLIC FUNCTIONS - START
// -------------------------
int SealighterSession::run
(
    const std::string& config_string
)
{
    int status = ERROR_SUCCESS;

    // Setup Event Logging
    status = EventRegisterSealighter();
    if (ERROR_SUCCESS != status) {
        loggr.info("Error registering event log: {}", status);
        return SEALIGHTER_ERROR_EVENTLOG_REGISTER;
    }

    // Set the active session for the Ctrl+C handler
    g_active_session = this;

    // Parse config file
    status = this->parse_config(config_string);
    if (ERROR_SUCCESS != status) {
        return status;
    }

    if (!user_session_ && !kernel_session_) {
        loggr.info("Failed to define any ETW Session");
        return SEALIGHTER_ERROR_NO_SESSION_CREATED;
    }

    // Create a thread to wait for the event StopSealighter
    auto waitForStop = std::jthread([this]() { this->WaitForStopEvent(); });

    // Add ctrl+C handler for graceful shutdown when running standalone
    // with a console. When launched by hsagent (no console, CREATE_SUSPENDED),
    // this fails — shutdown is handled via the Local\StopSealighter event.
    if (!SetConsoleCtrlHandler(crl_c_handler, TRUE)) {
        loggr.info("warning: failed to set ctrl-c handler (no console — shutdown via StopSealighter event)");
    }

    // Setup Buffering thread if needed
    this->start_bufferring();

    // Ensure cleanup always runs, even if run_trace throws
    try {
        // Start Trace we've configured
        // Don't run multithreaded if we don't have to
        if (user_session_ && !kernel_session_) {
            loggr.info("Starting User Trace...");
            loggr.info("-----------------------------------------");
            SetSealighterStartedEvent();
            this->run_trace(user_session_.get());
        }
        else if (!user_session_ && kernel_session_) {
            loggr.info("Starting Kernel Trace...");
            loggr.info("-----------------------------------------");
            SetSealighterStartedEvent();
            this->run_trace(kernel_session_.get());
        }
        else {
            // Have to multi-thread it
            loggr.info("Starting User and Kernel Traces...");
            loggr.info("-----------------------------------------");
            SetSealighterStartedEvent();
            std::thread user_thread = std::thread(&SealighterSession::run_trace<krabs::details::ut>, this, user_session_.get());
            std::thread kernel_thread = std::thread(&SealighterSession::run_trace<krabs::details::kt>, this, kernel_session_.get());

            // Call join, blocking until both have shut down
            user_thread.join();
            kernel_thread.join();
        }
    }
    catch (const std::exception& e) {
        loggr.info("Trace ended with error: {}", e.what());
        status = SEALIGHTER_ERROR_TRACE_STOP;
    }

    // All trace processing has finished (ProcessTrace returned on every
    // trace thread), so the traces have truly stopped now.
    loggr.info("All traces stopped, cleaning up...");

    // Teardown and cleanup
    this->stop_bufferring();
    this->teardown_logger_file();
    (void)EventUnregisterSealighter();

    // Signal the stop event so the WaitForStopEvent jthread can exit.
    // Without this, Ctrl+C shutdown leaves the jthread blocked on Wait()
    // and the jthread destructor's join() hangs the process.
    try {
        winxx::NamedEvent<wchar_t> evtStop{ EVENT_STOP_SEALIGHTER.data(), EVENT_MODIFY_STATE, FALSE };
        evtStop.Set();
    }
    catch (...) {
        // Event may not exist if jthread hasn't created it yet — safe to ignore
    }

    if (stop_event_error_.load() != 0) {
        return stop_event_error_.load();
    }

    return status;
}
