#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>
#include <cstdlib>

#include "logger.h"
#include "sealighter_handler.h"
#include "sealighter_errors.h"
#include "sealighter_util.h"
#include "sealighter_controller.h"

constexpr std::string_view APP_NAME{ "Sealighter" };
constexpr std::uintmax_t MAX_CONFIG_FILE_SIZE = 10 * 1024 * 1024;
#define loggr (logger::Logger::GetInstance().logger())

static std::string get_log_path()
{
    char* env = nullptr;
    size_t env_len = 0;
    _dupenv_s(&env, &env_len, "SEALIGHTER_LOG_PATH");
    if (env && *env) {
        std::string path(env);
        free(env);
        return path;
    }
    free(env);
    auto dir = std::filesystem::temp_directory_path() / "Sealighter";
    std::filesystem::create_directories(dir);
    return (dir / "sealighter.log").string();
}

/*
    Main entrypoint
*/
int main
(
    int argc,
    char* argv[]
)
{
    auto log_path = get_log_path();
    logger::Logger::GetInstance().init(APP_NAME, log_path);
    int status = 0;
    if (2 != argc) {
        loggr.info("usage: {}", argv[0]);
        return SEALIGHTER_ERROR_NOCONFIG;
    }

    std::string config_path = argv[1];

    if (!file_exists(config_path)) {
        loggr.info("Error: Config file doesn't exist");
        return SEALIGHTER_ERROR_MISSING_CONFIG;
    }

    auto file_size = std::filesystem::file_size(config_path);
    if (file_size > MAX_CONFIG_FILE_SIZE) {
        loggr.info("Error: Config file too large ({} bytes, max {})",
            static_cast<unsigned long long>(file_size),
            static_cast<unsigned long long>(MAX_CONFIG_FILE_SIZE));
        return SEALIGHTER_ERROR_MISSING_CONFIG;
    }

    std::ifstream  config_stream(config_path);
    std::string config_string((std::istreambuf_iterator<char>(config_stream)),
        (std::istreambuf_iterator<char>()));
    config_stream.close();

    SealighterSession session;
    status = session.run(config_string);

    return status;
}
