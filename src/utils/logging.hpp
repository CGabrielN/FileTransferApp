#pragma once

#include <string>
#include <memory>
#include <spdlog/spdlog.h>

namespace utils {

    /**
     * Logging utility class
     */
    class Logging {
    public:
        /**
        * Initialize the logging system
        * @param appName Name of the application (used for log files)
        * @param logToFile Whether to log to a file (default: true)
        * @param logToConsole Whether to log to the console (default: true)
        * @param logLevel The log level to use (default: info)
        */
        static void init(const std::string &appName,
                         bool logToFile = true,
                         bool logToConsole = true,
                         spdlog::level::level_enum logLevel = spdlog::level::info);

        /**
         * Set the global log level
         * @param level The log level to set
         */
        static void setLogLevel(spdlog::level::level_enum level);

        /**
         * Get the path to the log directory
         * @return The log directory path
         */
        static std::string getLogDirectory();

        /**
         * Flush all log outputs
         */
        static void flush();

        /**
         * Shutdown the logging system
         */
        static void shutdown();
    };
}