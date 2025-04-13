#include "logging.hpp"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <filesystem>

#ifdef PLATFORM_WINDOWS

#include <windows.h>
#include <shlobj.h>
#include <iostream>

#elif defined(PLATFORM_MACOS) || defined(PLATFORM_LINUX)
#include <pwd.h>
#include <unistd.h>
#include <sys/types.h>
#endif

namespace utils {
    namespace {
        // Maximum log file size in bytes (= 1 MB)
        constexpr size_t
                MAX_LOG_FILE_SIZE = 1024 * 1024;

        // Maximum number of logfiles to keep
        constexpr size_t
                MAX_LOG_FILES = 5;

        // Get the path to the user's home directory
        std::string getUserHomeDirectory() {
#ifdef PLATFORM_WINDOWS
            wchar_t path[MAX_PATH];
            if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PROFILE, nullptr, 0, path))) {
                std::wstring wPath(path);
                return {wPath.begin(), wPath.end()};
            }
            return "";
#else
            struct passwd *pw = getpwuid(getuid());
            const char *homeDir = pw->pw_dir;
            if (homeDir) {
                return homeDir;
            }
            return "";
#endif
        }

        // Get the path to logs directory
        std::string getLogsPath(const std::string &appName) {
            std::string homeDir = getUserHomeDirectory();
            if (homeDir.empty()) {
                // fallback to current directory
                return std::filesystem::current_path().string() + "/logs";
            }

#ifdef PLATFORM_WINDOWS
            return homeDir + R"(\AppData\Local\)" + appName + "\\logs";
#elif defined(PLATFORM_MACOS)
            return homeDir + "/Library/logs" + appName;
#else // Linux and others
            return homeDir + "/.local/share/" + appName + "/logs";
#endif
        }
    }

    void
    Logging::init(const std::string &appName, bool logToFile, bool logToConsole, spdlog::level::level_enum logLevel) {
        try {
            // Create a vector of sinks
            std::vector<spdlog::sink_ptr> sinks;

            // Add console sink if requested
            if (logToConsole) {
                auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
                consoleSink->set_level(logLevel);
                consoleSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");
                sinks.push_back(consoleSink);
            }

            // Add file sink if requested
            if (logToFile) {
                std::string logDir = getLogsPath(appName);
                std::filesystem::create_directories(logDir); // Create the directory if it doesn't exist

                std::string logFilePath = logDir + "/" + appName + ".log";

                auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                        logFilePath, MAX_LOG_FILE_SIZE, MAX_LOG_FILES);
                fileSink->set_level(logLevel);
                fileSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] %v");
                sinks.push_back(fileSink);
            }

            // Create and register the logger
            auto logger = std::make_shared<spdlog::logger>(appName, sinks.begin(), sinks.end());
            logger->set_level(logLevel);

            // Set as default logger
            spdlog::set_default_logger(logger);

            // Log initialization
            spdlog::info("Logging initialized for application: {}", appName);
        } catch (const spdlog::spdlog_ex &ex) {
            std::cerr << "Log initialization failed: " << ex.what() << std::endl;
        }
    }

    void Logging::setLogLevel(spdlog::level::level_enum level) {
        spdlog::set_level(level);
    }

    std::string Logging::getLogDirectory() {
        // Get the default logger
        auto logger = spdlog::default_logger();

        // Try to find a file sink
        for (const auto &sink: logger->sinks()) {
            auto fileSink = std::dynamic_pointer_cast<spdlog::sinks::rotating_file_sink_mt>(sink);
            if (fileSink) {
                // Get the filename from the sink
                std::string filename = fileSink->filename();
                return std::filesystem::path(filename).parent_path().string();
            }
        }

        return "";
    }

    void Logging::flush() {
        spdlog::default_logger()->flush();
    }

    void Logging::shutdown() {
        spdlog::shutdown();
    }
}
