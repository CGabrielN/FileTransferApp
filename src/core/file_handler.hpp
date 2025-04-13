#pragma once

#include "../platform/platform.hpp"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace core {

    /**
     * Structure to hold file info
     */
    struct FileInfo {
        std::string name;
        std::string path;
        std::uintmax_t size;
        std::string lastModified;
        std::string mimeType;

        // For serialization and deserialization
        nlohmann::json toJson() const;

        static FileInfo fromJson(const nlohmann::json &json);
    };

    /**
     * Callback for progress updates during file operations
     * @param bytesProcessed Number of bytes processed so far
     * @param totalBytes Total number of bytes to process
     * @param fileName Name of the file being processed
     */
    using ProgressCallback = std::function<
            void(std::uintmax_t
                 bytesProcessed,
                 std::uintmax_t totalBytes,
                 const std::string &fileName
            )>;


    class FileHandler {
    public:
        /**
         * Constructor
         * @param platform Platform-specific implementation
         */
        explicit FileHandler(std::shared_ptr<platform::Platform> platform);

        /**
         * Get information about a file
         * @param filePath Path to the file
         * @return FileInfo structure with file metadata
         */
        FileInfo getFileInfo(const std::string &filePath) const;

        /**
         * Read a file into memory
         * @param filePath Path to the file
         * @param progressCallback Optional callback for progress updates
         * @return Vector of bytes containing the file data
         */
        std::vector<uint8_t> readFile(const std::string &filePath,
                                      const ProgressCallback &progressCallback = nullptr) const;

        /**
         * Write data to a file
         * @param filePath Path where the file should be written
         * @param data Data to write to the file
         * @param progressCallback Optional callback for progress updates
         * @return True if the write operation was successful, false otherwise
         */
        bool writeFile(const std::string &filePath,
                       const std::vector<uint8_t> &data,
                       const ProgressCallback &progressCallback = nullptr) const;

        /**
         * Check if a file exists
         * @param filePath Path to the file
         * @return True if the file exists, false otherwise
         */
        bool fileExists(const std::string &filePath) const;

        /**
         * Get the default download directory for the current platform
         * @return Path to the default download directory
         */
        std::string getDefaultDownloadDirectory() const;

        /**
         * Open a file with the default application
         * @param filePath Path to the file
         * @return True if the file was successfully opened, false otherwise
         */
        bool openFile(const std::string &filePath) const;

        /**
         * Get a unique filename by appending a number if the file already exists
         * @param directory Directory where the file will be saved
         * @param filename Original filename
         * @return Unique filename
         */
        std::string getUniqueFilename(const std::string &directory,
                                      const std::string &filename) const;

    private:
        std::shared_ptr<platform::Platform> m_platform;

        /**
         * Detect the MIME type of a file
         * @param filePath Path to the file
         * @return MIME type as string
         */
        std::string detectMimeType(const std::string &filePath) const;
    };

}
