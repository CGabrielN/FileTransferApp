#include "file_handler.hpp"
#include "../utils/logging.hpp"

#include <fstream>
#include <sstream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <filesystem>
#include <algorithm>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace core {

    // FileInfo JSON serialization/deserialization
    json FileInfo::toJson() const {
        return {
                {"name",         name},
                {"path",         path},
                {"size",         size},
                {"lastModified", lastModified},
                {"mimeType",     mimeType}
        };
    }

    FileInfo FileInfo::fromJson(const json &j) {
        FileInfo info;
        info.name = j["name"].get<std::string>();
        info.path = j["path"].get<std::string>();
        info.size = j["size"].get<std::uintmax_t>();
        info.lastModified = j["lastModified"].get<std::string>();
        info.mimeType = j["mimeType"].get<std::string>();
        return info;
    }

    FileHandler::FileHandler(std::shared_ptr<platform::Platform> platform) : m_platform(std::move(platform)) {
        SPDLOG_DEBUG("FileHandler initialized");
    }

    FileInfo FileHandler::getFileInfo(const std::string &filePath) const {
        FileInfo info;

        try {
            fs::path path(filePath);
            if (!fs::exists(path)) {
                throw std::runtime_error("File doesn't exist: " + filePath);
            }

            info.name = path.filename().string();
            info.path = fs::absolute(path).string();
            info.size = fs::file_size(path);
            info.mimeType = detectMimeType(filePath);

            // Get last modified time
            auto lastWriteTime = fs::last_write_time(path);
            auto systemTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                    lastWriteTime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());

            auto time = std::chrono::system_clock::to_time_t(systemTime);
            std::stringstream ss;
            ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
            info.lastModified = ss.str();

            SPDLOG_DEBUG("File info retrieved for {}: {} bytes", info.name, info.size);

        } catch (const std::exception &e) {
            SPDLOG_ERROR("Error getting file info for {}: {}", filePath, e.what());
            throw;
        }

        return info;
    }


    std::vector<uint8_t>
    FileHandler::readFile(const std::string &filePath, const core::ProgressCallback &progressCallback) const {
        try {
            // Get file size for progress report
            std::uintmax_t fileSize = fs::file_size(filePath);

            // Open file in binary mode
            std::ifstream file(filePath, std::ios::binary);
            if (!file) {
                throw std::runtime_error("Failed to open file for reading: " + filePath);
            }

            // Read file into vector
            std::vector<uint8_t> buffer(fileSize);

            // Define chunk size for progress reporting
            constexpr std::size_t chunkSize = 1024 * 1024;
            std::size_t bytesRead = 0;

            if (progressCallback) {
                progressCallback(0, fileSize, fs::path(filePath).filename().string());
            }

            while (bytesRead < fileSize) {
                std::size_t bytesToRead = std::min(chunkSize, fileSize - bytesRead);
                file.read(reinterpret_cast<char *>(buffer.data() + bytesRead),
                          static_cast<std::streamsize>(bytesToRead));

                if (file.fail() && !file.eof()) {
                    throw std::runtime_error("Error reading file: " + filePath);
                }

                bytesRead += bytesToRead;

                if (progressCallback) {
                    progressCallback(bytesRead, fileSize, fs::path(filePath).filename().string());
                }
            }

            file.close();
            SPDLOG_DEBUG("File read complete: {} ({} bytes)", filePath, fileSize);
            return buffer;

        } catch (const std::exception &e) {
            SPDLOG_ERROR("Error reading file {}: {}", filePath, e.what());
            throw;
        }
    }

    bool FileHandler::writeFile(const std::string &filePath, const std::vector<uint8_t> &data,
                                const core::ProgressCallback &progressCallback) const {
        try {
            // Create directories if they don't exist
            fs::path path(filePath);
            if (!path.parent_path().empty()) {
                fs::create_directories(path.parent_path());
            }

            // Open file for writing in binary mode
            std::ofstream file(filePath, std::ios::binary);
            if (!file) {
                throw std::runtime_error("Failed to open file for writing: " + filePath);
            }

            std::size_t totalSize = data.size();

            if (progressCallback) {
                progressCallback(0, totalSize, fs::path(filePath).filename().string());
            }

            // Define chunk size for progress reporting
            constexpr std::size_t chunkSize = 1024 * 1024;
            std::size_t bytesWritten = 0;

            while (bytesWritten < totalSize) {
                std::size_t bytesToWrite = std::min(chunkSize, totalSize - bytesWritten);
                file.write(reinterpret_cast<const char *>(data.data() + bytesWritten),
                           static_cast<std::streamsize>(bytesToWrite));

                if (file.fail()) {
                    throw std::runtime_error("Error writing to file: " + filePath);
                }

                bytesWritten += bytesToWrite;

                if (progressCallback) {
                    progressCallback(bytesWritten, totalSize, fs::path(filePath).filename().string());
                }
            }

            file.close();
            SPDLOG_DEBUG("File write complete: {} ({} bytes)", filePath, totalSize);
            return true;

        } catch (const std::exception &e) {
            SPDLOG_ERROR("Error writing file {}: {}", filePath, e.what());
            return false;
        }
    }

    bool FileHandler::fileExists(const std::string &filePath) const {
        return fs::exists(filePath);
    }

    std::string FileHandler::getDefaultDownloadDirectory() const {
        return m_platform->getDefaultDownloadDirectory();
    }

    bool FileHandler::openFile(const std::string &filePath) const {
        return m_platform->openFile(filePath);
    }

    std::string FileHandler::getUniqueFilename(const std::string &directory, const std::string &filename) const {
        fs::path dir(directory);
        fs::path origFilename(filename);

        // If the file doesn't exist, we can use the original filename
        if (!fs::exists(dir / origFilename)) {
            return filename;
        }

        std::string baseName = origFilename.stem().string();
        std::string extension = origFilename.extension().string();

        // Try adding numbers to the filename until we find one that doesn't exist
        int counter = 1;
        std::string newFilename;

        do {
            std::stringstream ss;
            ss << baseName << "_" << counter++ << extension;
            newFilename = ss.str();
        } while (fs::exists(dir / newFilename));

        return newFilename;
    }

    std::string FileHandler::detectMimeType(const std::string &filePath) const {
        // Simple MIME type detection based on file extension
        std::string extension = fs::path(filePath).extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        // Common MIME types
        if (extension == ".txt") return "text/plain";
        if (extension == ".html" || extension == ".htm") return "text/html";
        if (extension == ".css") return "text/css";
        if (extension == ".js") return "text/javascript";
        if (extension == ".json") return "application/json";
        if (extension == ".xml") return "application/xml";
        if (extension == ".pdf") return "application/pdf";
        if (extension == ".zip") return "application/zip";
        if (extension == ".doc") return "application/msword";
        if (extension == ".docx") return "application/vnd.openxmlformats-officedocument.wordprocessingml.document";
        if (extension == ".xls") return "application/vnd.ms-excel";
        if (extension == ".xlsx") return "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet";
        if (extension == ".ppt") return "application/vnd.ms-powerpoint";
        if (extension == ".pptx") return "application/vnd.openxmlformats-officedocument.presentationml.presentation";
        if (extension == ".jpg" || extension == ".jpeg") return "image/jpeg";
        if (extension == ".png") return "image/png";
        if (extension == ".gif") return "image/gif";
        if (extension == ".svg") return "image/svg+xml";
        if (extension == ".mp3") return "audio/mpeg";
        if (extension == ".mp4") return "video/mp4";
        if (extension == ".avi") return "video/x-msvideo";
        if (extension == ".wav") return "audio/wav";
        if (extension == ".ogg") return "audio/ogg";
        if (extension == ".webm") return "video/webm";

        // Default MIME type for unknown extensions
        return "application/octet-stream";
    }

}
