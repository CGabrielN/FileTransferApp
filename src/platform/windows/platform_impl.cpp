
#include "../platform.hpp"
#include "../../utils/logging.hpp"

#include <spdlog/spdlog.h>
#include <iostream>
#include <sstream>
#include <filesystem>
#include <memory>

// Windows specific includes
#define WIN32_LEAN_AND_MEAN  // Reduce Windows header bloat

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <ShlObj.h>
#include <KnownFolders.h>
#include <shellapi.h>

// Link with required libraries
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")


namespace platform {

    class WindowsPlatform : public Platform {
    public:
        WindowsPlatform() {
            // Initialize Winsock
            WSADATA wsaData;
            if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
                SPDLOG_ERROR("Failed to initialize Winsock");
                throw std::runtime_error("Failed to initialize Winsock");
            }
            SPDLOG_DEBUG("Winsock initialized");
        }

        ~WindowsPlatform() override {
            // Cleanup Winsock
            WSACleanup();
            SPDLOG_DEBUG("Winsock cleanup complete");
        }

        [[nodiscard]] std::string getName() const override {
            return "Windows";
        }

        [[nodiscard]] std::string getDefaultDownloadDirectory() const override {
            PWSTR downloadPath = nullptr;
            if (SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &downloadPath) == S_OK) {
                std::wstring wpath(downloadPath);
                // Convert wide string to narrow string
                std::string result(wpath.begin(), wpath.end());
                CoTaskMemFree(downloadPath);
                return result;
            }
            // Fallback to Documents folder if Downloads is not available
            wchar_t docPath[MAX_PATH];
            if (SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, 0, docPath) == S_OK) {
                std::wstring wpath(docPath);
                std::string result(wpath.begin(), wpath.end());
                return result;
            }

            // Last resort fallback
            return std::filesystem::current_path().string();
        }

        [[nodiscard]] bool supportsFeature(const std::string &featureName) const override {
            // List of features supported on windows
            if (featureName == "drag_and_drop" ||
                featureName == "notification" ||
                featureName == "auto_discovery") {
                return true;
            }
            return false;
        }

        [[nodiscard]] std::vector<std::string> getNetworkInterfaces() const override {
            std::vector<std::string> interfaces;

            // Get adapter info
            ULONG bufferSize = 15000; // start with a 15K buffer

            PIP_ADAPTER_ADDRESSES pAddresses = nullptr;
            ULONG flags = GAA_FLAG_INCLUDE_PREFIX;

            ULONG result = ERROR_BUFFER_OVERFLOW;

            // Allocate memory for the buffer
            do {
                pAddresses = (IP_ADAPTER_ADDRESSES *) malloc(bufferSize);
                if (pAddresses == nullptr) {
                    SPDLOG_ERROR("Memory allocation failed for IP_ADAPTER_ADDRESSES struct");
                    return interfaces;
                }

                result = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, pAddresses, &bufferSize);

                if (result == ERROR_BUFFER_OVERFLOW) {
                    free(pAddresses);
                    pAddresses = nullptr;
                    bufferSize *= 2;  // Double the buffer size and try again
                }
            } while (result == ERROR_BUFFER_OVERFLOW);

            if (result == NO_ERROR) {
                PIP_ADAPTER_ADDRESSES pCurrent = pAddresses;
                while (pCurrent) {
                    // Skip loopback and tunnel interfaces
                    if (pCurrent->IfType != IF_TYPE_SOFTWARE_LOOPBACK &&
                        pCurrent->OperStatus == IfOperStatusUp) {
                        // Convert from wide char to narrow string
                        std::wstring wName(pCurrent->FriendlyName);
                        std::string name(wName.begin(), wName.end());
                        interfaces.push_back(name);
                    }
                    pCurrent = pCurrent->Next;
                }
            } else {
                SPDLOG_ERROR("GetAdaptersAddresses failed with error code: {}", result);
            }

            free(pAddresses);

            return interfaces;
        }

        [[nodiscard]] std::string getInterfaceAddress(const std::string &interfaceName) const override {
            // Convert interfaceName to wide string for comparison
            std::wstring wInterfaceName(interfaceName.begin(), interfaceName.end());

            ULONG bufferSize = 15000;
            PIP_ADAPTER_ADDRESSES pAddresses = nullptr;
            ULONG flags = GAA_FLAG_INCLUDE_PREFIX;

            pAddresses = (IP_ADAPTER_ADDRESSES *) malloc(bufferSize);
            if (pAddresses == nullptr) {
                SPDLOG_ERROR("Memory allocation failed for IP_ADAPTER_ADDRESSES struct");
                return "";
            }

            ULONG result = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, pAddresses, &bufferSize);

            if (result == NO_ERROR) {
                PIP_ADAPTER_ADDRESSES pCurrent = pAddresses;
                while (pCurrent) {
                    std::wstring wName(pCurrent->FriendlyName);

                    if (wName == wInterfaceName) {
                        // Found our interface, now get its IP
                        PIP_ADAPTER_UNICAST_ADDRESS pUnicast = pCurrent->FirstUnicastAddress;
                        while (pUnicast) {
                            if (pUnicast->Address.lpSockaddr->sa_family == AF_INET) {
                                // IPv4 address
                                auto *ipv4 = reinterpret_cast<struct sockaddr_in *>(pUnicast->Address.lpSockaddr);
                                char ipStr[INET_ADDRSTRLEN];
                                inet_ntop(AF_INET, &(ipv4->sin_addr), ipStr, INET_ADDRSTRLEN);
                                free(pAddresses);
                                return {ipStr};
                            }
                            pUnicast = pUnicast->Next;
                        }
                    }
                    pCurrent = pCurrent->Next;
                }
            }

            free(pAddresses);

            return "";
        }

        [[nodiscard]] bool openFile(const std::string &filePath) const override {
            // Use ShellExecute to open the file with the default handler
            HINSTANCE result = ShellExecuteA(nullptr, "open", filePath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);

            // ShellExecute returns a value greater than 32 if successful
            return reinterpret_cast<intptr_t>(result) > 32;
        }
    };

    std::shared_ptr<Platform> PlatformFactory::create() {
#ifdef PLATFORM_WINDOWS
        return std::make_shared<WindowsPlatform>();
#else
#error "Platform implementation not available for this platform";
#endif
    }
}
