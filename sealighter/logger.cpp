#include "logger.h"

namespace logger {

namespace {

spdlog::logger GetConsoleFileLogger(std::string_view appName, std::string_view logPath)
{
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_level(spdlog::level::info);
    console_sink->set_pattern("%v");

    auto file_sink =
        std::make_shared<spdlog::sinks::basic_file_sink_mt>(std::string{ logPath }, true);
    file_sink->set_level(spdlog::level::trace);
    file_sink->set_pattern("[%H:%M:%S]\t[%l]\t%v");

    spdlog::logger logger{ appName.data(), { console_sink, file_sink } };
    logger.set_level(spdlog::level::debug);
    logger.flush_on(spdlog::level::debug);

    return logger;
}

spdlog::logger GetConsoleLogger(std::string_view appName)
{
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_level(spdlog::level::info);
    console_sink->set_pattern("%v");

    spdlog::logger logger{ appName.data(), console_sink };
    return logger;
}

} // !namespace

void Logger::init(std::string_view appName)
{
    if (logger_) {
        return;
    }

    logger_ = GetConsoleLogger(appName);
}

void Logger::init(std::string_view appName, std::string_view logPath)
{
    if (logger_) {
        return;
    }

    logger_ = GetConsoleFileLogger(appName, logPath);
}

spdlog::logger& Logger::logger() noexcept
{
    return *logger_;
}

} // !namespace logger
