#ifndef LOGGER_H
#define LOGGER_H

#define SPDLOG_WCHAR_TO_UTF8_SUPPORT

#include "spdlog/spdlog.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/basic_file_sink.h"

#include <string_view>
#include <optional>

namespace logger {

class Logger {
public:
    static Logger& GetInstance() noexcept
    {
        static Logger instance;
        return instance;
    }

    void init(std::string_view appName);
    void init(std::string_view appName, std::string_view);
    spdlog::logger& logger() noexcept;

private:
    Logger() = default;
    std::optional<spdlog::logger> logger_;
};

} // !namespace logger

#endif // !LOGGER_H
