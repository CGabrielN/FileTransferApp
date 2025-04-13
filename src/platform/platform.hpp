#pragma once

#include <string>
#include <memory>
#include <vector>

namespace platform {

    /**
     * Abstract platform interface that provides platform-specific functionality.
     */
    class Platform {
    public:
        virtual ~Platform() = default;

        /**
         * Get the platform name
         * @return String identifying the platform
         */
        [[nodiscard]] virtual std::string getName() const = 0;

        /**
         * Get the default download directory for the platform
         * @return Path to the default download directory
         */
        [[nodiscard]] virtual std::string getDefaultDownloadDirectory() const = 0;

        /**
         * Check if the platform supports a specific feature
         * @param featureName Name of the feature to check
         * @return True if the feature is supported, false otherwise
         */
        [[nodiscard]] virtual bool supportsFeature(const std::string &featureName) const = 0;

        /**
         * Get the network interfaces available on this platform
         * @return Vector of interfaces names
         */
        [[nodiscard]] virtual std::vector<std::string> getNetworkInterfaces() const = 0;

        /**
         * Get the IP address for a specific network interface
         * @param interfaceName Name of the interface
         * @return IP address as a string
         */
        [[nodiscard]] virtual std::string getInterfaceAddress(const std::string &interfaceName) const = 0;

        /**
         * Open a file using the platform's default handler
         * @param filePath Path to the file to opem
         * @return True if the file was successfully open, false otherwise
         */
        [[nodiscard]] virtual bool openFile(const std::string &filePath) const = 0;
    };

    class PlatformFactory {
    public:
        /**
         * Create the appropriate platform implementation for the current system
         * @return Shared pointer to the platform implementation
         */
        static std::shared_ptr<Platform> create();
    };
}