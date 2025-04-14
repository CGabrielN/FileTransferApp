#include "../platform.hpp"
#include "../../utils/logging.hpp"

#include <spdlog/spdlog.h>
#include <iostream>
#include <sstream>
#include <filesystem>
#include <memory>
#include <unistd.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <pwd.h>
#include <cstdlib>

namespace platform {

    class LinuxPlatform : public Platform {
    public:
        LinuxPlatform(){
            SPDLOG_DEBUG("Linux platform initialized");
        }

        ~LinuxPlatform() override {
            SPDLOG_DEBUG("Linux platform shutdown");
        }

        std::string getName() const override {
            return "Linux";
        }

        std::string getDefaultDownloadDirectory() const override {
            // Try to get XDG_DOWNLOAD_DIR first
            const char* xdgDownloads = std::getenv("XDG_DOWNLOAD_DIR");
            if (xdgDownloads != nullptr) {
                return xdgDownloads;
            }

            // Fall back to $HOME/Downloads
            const char* home = std::getenv("HOME");
            if (home != nullptr) {
                return std::string(home) + "/Downloads";
            }

            // Last resort: get home directory from passwd
            struct passwd* pw = getpwuid(getuid());
            if (pw != nullptr) {
                return std::string(pw->pw_dir) + "/Downloads";
            }

            // Ultimate fallback: current directory
            return std::filesystem::current_path().string();
        }

        bool supportsFeature(const std::string &featureName) const override {
            // List of features supported on Linux
            if (featureName == "drag_and_drop" ||
                featureName == "notification" ||
                featureName == "auto_discovery") {
                return true;
            }
            return false;
        }


        std::vector<std::string> getNetworkInterfaces() const override {
            std::vector<std::string> interfaces;

            struct ifaddrs* ifaddr;
            if (getifaddrs(&ifaddr) == -1) {
                SPDLOG_ERROR("Failed to get network interfaces: {}", strerror(errno));
                return interfaces;
            }

            // Walk through linked list, maintaining head pointer for later free
            for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
                // Skip if no address assigned or if loopback
                if (ifa->ifa_addr == nullptr || (ifa->ifa_flags & IFF_LOOPBACK)) {
                    continue;
                }

                // Only consider IPv4 interfaces
                if (ifa->ifa_addr->sa_family == AF_INET) {
                    interfaces.push_back(ifa->ifa_name);
                }
            }

            freeifaddrs(ifaddr);
            return interfaces;
        }

        std::string getInterfaceAddress(const std::string &interfaceName) const override {
            struct ifaddrs* ifaddr;
            std::string result;

            if (getifaddrs(&ifaddr) == -1) {
                SPDLOG_ERROR("Failed to get interface address: {}", strerror(errno));
                return result;
            }

            // Walk through linked list, finding the requested interface
            for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
                if (ifa->ifa_addr == nullptr) {
                    continue;
                }

                if (ifa->ifa_addr->sa_family == AF_INET && interfaceName == ifa->ifa_name) {
                    // Found the interface, get the address
                    struct sockaddr_in* addr = reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr);
                    char ip[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &(addr->sin_addr), ip, INET_ADDRSTRLEN);
                    result = ip;
                    break;
                }
            }

            freeifaddrs(ifaddr);
            return result;
        }

        bool openFile(const std::string &filePath) const override {
            // Use xdg-open to open the file with default application
            std::string command = "xdg-open \"" + filePath + "\" &";
            int result = system(command.c_str());
            return (result == 0);
        }
    };

    std::shared_ptr<Platform> PlatformFactory::create() {
#ifdef PLATFORM_LINUX
        return std::make_shared<LinuxPlatform>();
#else
#error "Platform implementation not available for this platform";
#endif
    }
}