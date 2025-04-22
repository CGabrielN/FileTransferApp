#include "transfer_manager.hpp"
#include "../utils/logging.hpp"
#include "../utils/encryption.hpp"

#include <spdlog/spdlog.h>
#include <chrono>
#include <random>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <fstream>


using json = nlohmann::json;
using namespace std::chrono;
namespace fs = std::filesystem;

namespace core {

    // TransferInfo serialization/deserialization
    json TransferInfo::toJson() const {
        return {
                {"id",               id},
                {"peerId",           peerId},
                {"peerName",         peerName},
                {"peerAddress",      peerAddress},
                {"direction",        static_cast<int>(direction)},
                {"status",           static_cast<int>(status)},
                {"filePath",         filePath},
                {"fileName",         fileName},
                {"fileSize",         fileSize},
                {"bytesTransferred", bytesTransferred},
                {"progress",         progress},
                {"startTime",        startTime},
                {"endTime",          endTime},
                {"errorMessage",     errorMessage}
        };
    }

    TransferInfo TransferInfo::fromJson(const nlohmann::json &j) {
        TransferInfo info;
        info.id = j["id"].get<std::string>();
        info.peerId = j["peerId"].get<std::string>();
        info.peerName = j["peerName"].get<std::string>();
        info.peerAddress = j["peerAddress"].get<std::string>();
        info.direction = static_cast<TransferDirection>(j["direction"].get<int>());
        info.status = static_cast<TransferStatus>(j["status"].get<int>());
        info.filePath = j["filePath"].get<std::string>();
        info.fileName = j["fileName"].get<std::string>();
        info.fileSize = j["fileSize"].get<std::uintmax_t>();
        info.bytesTransferred = j["bytesTransferred"].get<std::uintmax_t>();
        info.progress = j["progress"].get<float>();
        info.startTime = j["startTime"].get<int64_t>();
        info.endTime = j["endTime"].get<int64_t>();
        info.errorMessage = j["errorMessage"].get<std::string>();
        return info;
    }

    TransferManager::TransferManager(std::shared_ptr<FileHandler> fileHandler,
                                     std::shared_ptr<network::SocketHandler> socketHandler,
                                     std::shared_ptr<DiscoveryService> discoveryService, uint16_t serverPort)
            : m_fileHandler(std::move(fileHandler)), m_socketHandler(std::move(socketHandler)),
              m_discoveryService(std::move(discoveryService)), m_serverPort(serverPort), m_downloadDirectory(""),
              m_initialized(false), m_nextTransferId(1) {
        // Set default download directory
        m_downloadDirectory = m_fileHandler->getDefaultDownloadDirectory();

        SPDLOG_DEBUG("TransferManager initialized with server port: {}", m_serverPort);
    }


    TransferManager::~TransferManager() {
        shutdown();
    }

    bool TransferManager::init() {
        if (m_initialized.exchange(true)) {
            SPDLOG_WARN("TransferManager already initialized");
            return true;
        }

        SPDLOG_INFO("Initializing TransferManager");

        // Initialize TCP server for incoming transfers
        bool success = m_socketHandler->initTcpServer(m_serverPort,
                                                      [this](const std::vector<uint8_t> &data,
                                                             const std::string &endpoint) {
                                                          this->handleIncomingData(data, endpoint);
                                                      },
                                                      [this](network::ConnectionStatus status,
                                                             const std::string &endpoint,
                                                             const std::string &errorMessage) {
                                                          this->handleConnectionStatus(status, endpoint, errorMessage);
                                                      });

        if (!success) {
            SPDLOG_ERROR("Failed to initialize TCP server");
            m_initialized = false;
            return false;
        }

        m_initialized = true;

        SPDLOG_INFO("TransferManager initialized successfully");
        return true;
    }

    void TransferManager::shutdown() {
        if (!m_initialized.exchange(false)) {
            return; // Already shut down
        }

        SPDLOG_INFO("Shutting down TransferManager");

        // Cancel all active transfers
        std::vector<std::string> activeTransfers;

        {
            std::lock_guard<std::mutex> lock(m_transfersMutex);
            for (const auto &[id, transfer]: m_transfers) {
                if (transfer->status == TransferStatus::InProgress ||
                    transfer->status == TransferStatus::Initializing ||
                    transfer->status == TransferStatus::Waiting) {
                    activeTransfers.push_back(id);
                }
            }
        }

        for (const auto &id: activeTransfers) {
            cancelTransfer(id);
        }

        SPDLOG_INFO("TransferManager shutdown complete");
    }

    std::string TransferManager::sendFile(const std::string &peerId, const std::string &filePath) {
        if (!m_initialized) {
            SPDLOG_ERROR("TransferManager not initialized");
            return "";
        }

        // Check if file exists
        if (!m_fileHandler->fileExists(filePath)) {
            SPDLOG_ERROR("File doesn't exist: {}", filePath);
            return "";
        }

        // Get peer information
        auto peer = getPeerInfo(peerId);
        if (!peer) {
            SPDLOG_ERROR("Peer not found: {}", peerId);
            return "";
        }

        // Connect to peer if not already connected
        if (!connectToPeer(*peer)) {
            SPDLOG_ERROR("Failed to connect to peer: {}  ({})", peer->name, peer->id);
            return "";
        }

        try {
            // Get file information
            FileInfo fileInfo = m_fileHandler->getFileInfo(filePath);

            // Create a new transfer record
            auto transferId = generateTransferId();
            auto transfer = std::make_shared<TransferInfo>();

            transfer->id = transferId;
            transfer->peerId = peerId;
            transfer->peerName = peer->name;
            transfer->peerAddress = peer->ipAddress + ":" + std::to_string(peer->port);
            transfer->direction = TransferDirection::Outgoing;
            transfer->status = TransferStatus::Initializing;
            transfer->filePath = filePath;
            transfer->fileName = fileInfo.name;
            transfer->fileSize = fileInfo.size;
            transfer->bytesTransferred = 0;
            transfer->progress = 0.0f;
            transfer->startTime = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
            transfer->endTime = 0;

            // Store the transfer
            {
                std::lock_guard<std::mutex> lock(m_transfersMutex);
                m_transfers[transferId] = transfer;
            }

            // Notify the status callback
            if (m_statusCallback) {
                m_statusCallback(*transfer);
            }

            // Create and send transfer request message
            network::TransferRequestMessage request;
            request.transferId = transferId;
            request.senderId = m_discoveryService->getPeerId();
            request.senderName = m_discoveryService->getDisplayName();
            request.fileName = fileInfo.name;
            request.fileSize = fileInfo.size;
            request.fileHash = ""; //TODO: implement file hashing

            // Serialize and send the message
            auto data = network::Protocol::serialize(request);
            auto endpoint = peer->ipAddress + ":" + std::to_string(peer->port);

            auto sendFuture = m_socketHandler->sendTcp(endpoint, data);
            int result = sendFuture.get();

            if (result < 0) {
                SPDLOG_ERROR("Failed to send transfer request to {}", endpoint);

                // Update transfer status
                updateTransferStatus(transferId, TransferStatus::Failed, "Failed to send transfer request");

                return "";
            }

            // Update transfer status
            updateTransferStatus(transferId, TransferStatus::Waiting);

            SPDLOG_INFO("Transfer request sent to {}: {}", peer->name, fileInfo.name);

            return transferId;

        } catch (const std::exception &e) {
            SPDLOG_ERROR("Error sending file: {}", e.what());
            return "";
        }
    }


    bool TransferManager::cancelTransfer(const std::string &transferId) {
        if (!m_initialized) {
            SPDLOG_ERROR("TransferManager not initialized");
            return false;
        }

        auto transfer = findTransfer(transferId);
        if (!transfer) {
            SPDLOG_ERROR("Transfer not found: {}", transferId);
            return false;
        }

        // Only cancel if the transfer is active
        if (transfer->status != TransferStatus::InProgress &&
            transfer->status != TransferStatus::Initializing &&
            transfer->status != TransferStatus::Waiting) {
            SPDLOG_WARN("Transfer already completed or canceled: {}", transferId);
            return false;
        }

        try {
            // Create and send cancel message
            network::TransferCancelMessage cancel;
            cancel.transferId = transferId;
            cancel.reason = "Canceled by user";

            // Serialize and send the message
            auto data = network::Protocol::serialize(cancel);
            auto endpoint = transfer->peerAddress;

            auto sendFuture = m_socketHandler->sendTcp(endpoint, data);
            sendFuture.get(); // Wait for send to complete, but ignore result

            // Update transfer status
            updateTransferStatus(transferId, TransferStatus::Canceled, "Canceled by user");

            SPDLOG_INFO("Transfer canceled: {}", transferId);

            return true;

        } catch (const std::exception &e) {
            SPDLOG_ERROR("Error canceling transfer: {}", e.what());

            // Update transfer status even if we couldn't send the cancel message
            updateTransferStatus(transferId, TransferStatus::Canceled,
                                 "Canceled by user (error: " + std::string(e.what()) + ")");

            return true;
        }
    }


    std::shared_ptr<TransferInfo> TransferManager::getTransferInfo(const std::string &transferId) const {
        std::lock_guard<std::mutex> lock(m_transfersMutex);

        auto it = m_transfers.find(transferId);
        if (it != m_transfers.end()) {
            return it->second;
        }

        return nullptr;
    }

    std::vector<TransferInfo> TransferManager::getAllTransfers() const {
        std::lock_guard<std::mutex> lock(m_transfersMutex);

        std::vector<TransferInfo> result;
        result.reserve(m_transfers.size());

        for (const auto &[id, transfer]: m_transfers) {
            result.push_back(*transfer);
        }

        return result;
    }

    void TransferManager::registerStatusCallback(TransferStatusCallback callback) {
        m_statusCallback = std::move(callback);
    }

    void TransferManager::registerRequestCallback(TransferRequestCallback callback) {
        m_requestCallback = std::move(callback);
    }

    std::string TransferManager::getDefaultDownloadDirectory() const {
        return m_downloadDirectory;
    }

    void TransferManager::setDefaultDownloadDirectory(const std::string &directory) {
        m_downloadDirectory = directory;
        SPDLOG_DEBUG("Default download directory set to: {}", m_downloadDirectory);
    }

    void TransferManager::handleIncomingData(const std::vector<uint8_t> &data, const std::string &endpoint) {
        try {
            // Deserialize the message
            auto message = network::Protocol::deserialize(data);

            // Process based on message type
            switch (message->type) {
                case network::MessageType::TransferRequest: {
                    auto request = dynamic_cast<network::TransferRequestMessage *>(message.get());
                    processTransferRequest(*request, endpoint);
                    break;
                }
                case network::MessageType::TransferResponse: {
                    auto response = dynamic_cast<network::TransferResponseMessage *>(message.get());
                    processTransferResponse(*response, endpoint);
                    break;
                }
                case network::MessageType::FileData: {
                    auto fileData = dynamic_cast<network::FileDataMessage *>(message.get());
                    processFileData(*fileData, endpoint);
                    break;
                }
                case network::MessageType::TransferComplete: {
                    auto complete = dynamic_cast<network::TransferCompleteMessage *>(message.get());
                    processTransferComplete(*complete, endpoint);
                    break;
                }
                case network::MessageType::TransferCancel: {
                    auto cancel = dynamic_cast<network::TransferCancelMessage *>(message.get());
                    processTransferCancel(*cancel, endpoint);
                    break;
                }
                default:
                    SPDLOG_ERROR("Unknown message type from {}", endpoint);
                    break;
            }

        } catch (const std::exception &e) {
            SPDLOG_ERROR("Error processing message from {}: {}", endpoint, e.what());
        }
    }


    void TransferManager::handleConnectionStatus(network::ConnectionStatus status, const std::string &endpoint,
                                                 const std::string &errorMessage) {
        // Find transfers associated with this endpoint
        auto transfer = findTransferByEndpoint(endpoint);

        if (!transfer) {
            // No transfer associated with this endpoint
            return;
        }

        switch (status) {
            case network::ConnectionStatus::Connected:
                SPDLOG_DEBUG("Connection established for transfer {}", transfer->id);
                break;

            case network::ConnectionStatus::Disconnected:
                SPDLOG_INFO("Connection closed for transfer {}", transfer->id);

                // If the transfer was still in progress, mark it as failed
                if (transfer->status == TransferStatus::InProgress ||
                    transfer->status == TransferStatus::Initializing ||
                    transfer->status == TransferStatus::Waiting) {
                    updateTransferStatus(transfer->id, TransferStatus::Failed, "Connection closed unexpectedly");
                }
                break;

            case network::ConnectionStatus::Error:
                SPDLOG_ERROR("Connection error for transfer {}: {}", transfer->id, errorMessage);

                // Mark the transfer as failed
                updateTransferStatus(transfer->id, TransferStatus::Failed, "Connection error: " + errorMessage);
                break;
        }
    }

    void TransferManager::processTransferRequest(const network::TransferRequestMessage &request,
                                                 const std::string &endpoint) {
        SPDLOG_INFO("Transfer request received from {} for file: {} ({})",
                    request.senderName, request.fileName, request.fileSize);

        // Create a new transfer record
        auto transfer = std::make_shared<TransferInfo>();

        transfer->id = request.transferId;
        transfer->peerId = request.senderId;
        transfer->peerName = request.senderName;
        transfer->peerAddress = endpoint;
        transfer->direction = TransferDirection::Incoming;
        transfer->status = TransferStatus::Waiting;
        transfer->fileName = request.fileName;
        transfer->fileSize = request.fileSize;
        transfer->bytesTransferred = 0;
        transfer->progress = 0.0f;
        transfer->startTime = duration_cast<milliseconds>(
                system_clock::now().time_since_epoch()).count();
        transfer->endTime = 0;

        // Generate file path
        //TODO: fix this conversion
//        std::string filePath = fs::path(m_downloadDirectory) /
//                               m_fileHandler->getUniqueFilename(m_downloadDirectory, request.fileName);
        std::string filePath = "";

        transfer->filePath = filePath;

        // Store the transfer
        {
            std::lock_guard<std::mutex> lock(m_transfersMutex);
            m_transfers[request.transferId] = transfer;
        }

        // Check if we should accept the transfer
        bool accepted = true;

        if (m_requestCallback) {
            accepted = m_requestCallback(*transfer);
        }

        // Create and send response message
        network::TransferResponseMessage response;
        response.transferId = request.transferId;
        response.accepted = accepted;
        response.receiverId = m_discoveryService->getPeerId();
        response.receiverName = m_discoveryService->getDisplayName();
        response.filePath = filePath;

        // Serialize and send the message
        auto data = network::Protocol::serialize(response);

        auto sendFuture = m_socketHandler->sendTcp(endpoint, data);
        int result = sendFuture.get();

        if (result < 0) {
            SPDLOG_ERROR("Failed to send transfer response to {}", endpoint);

            // Update transfer status
            updateTransferStatus(request.transferId, TransferStatus::Failed, "Failed to send transfer response");

            return;
        }

        // Update transfer status based on response
        if (accepted) {
            updateTransferStatus(request.transferId, TransferStatus::Waiting, "Waiting for file data");
            SPDLOG_INFO("Transfer accepted: {}", request.transferId);
        } else {
            updateTransferStatus(request.transferId, TransferStatus::Canceled, "Transfer rejected by user");
            SPDLOG_INFO("Transfer rejected: {}", request.transferId);
        }
    }


    void TransferManager::processTransferResponse(const network::TransferResponseMessage &response,
                                                  const std::string &endpoint) {
        auto transfer = findTransfer(response.transferId);

        if (!transfer) {
            SPDLOG_ERROR("Received response for unknown transfer: {}", response.transferId);
            return;
        }

        SPDLOG_INFO("Transfer response received from {}: {}",
                    response.receiverName, response.accepted ? "Accepted" : "Rejected");

        if (!response.accepted) {
            // Transfer was rejected
            updateTransferStatus(response.transferId, TransferStatus::Canceled, "Transfer rejected by recipient");
            return;
        }

        // Transfer was accepted, begin sending file data
        updateTransferStatus(response.transferId, TransferStatus::InProgress);

        // Start a new thread to handle the file transfer
        std::thread transferThread([this, transfer, endpoint]() {
            try {
                // Calculate file hash before transfer
                std::string fileHash;
#ifdef ENABLE_ENCRYPTION
                fileHash = utils::Encryption::calculateFileHash(transfer->filePath);
                SPDLOG_DEBUG("File hash calculated: {}", fileHash);
#endif

                // Read the file
                std::vector<uint8_t> fileData = m_fileHandler->readFile(
                        transfer->filePath,
                        [this, transfer](std::uintmax_t bytesProcessed, std::uintmax_t totalBytes,
                                         const std::string &fileName) {
                            // Update progress during file read
                            float progress = static_cast<float>(bytesProcessed) / totalBytes * 50.0f; // 50% for read
                            updateTransferProgress(transfer->id, bytesProcessed);
//                            updateTransferProgress(transfer->id, bytesProcessed);
                        }
                );

                // Encrypt file data if encryption is enabled
#ifdef ENABLE_ENCRYPTION
                if (!m_encryptionPassword.empty()) {
                    SPDLOG_INFO("Encrypting file data for transfer: {}", transfer->id);
                    std::vector<uint8_t> encryptedData;
                    if (utils::Encryption::encrypt(fileData, m_encryptionPassword, encryptedData)) {
                        fileData = std::move(encryptedData);
                        SPDLOG_INFO("File data encrypted successfully: {} -> {} bytes",
                                    transfer->fileSize, fileData.size());
                    } else {
                        SPDLOG_ERROR("Failed to encrypt file data, continuing with unencrypted transfer");
                    }
                }
#endif

                // Calculate the number of chunks needed
                constexpr std::size_t chunkSize = 1024 * 1024; // 1MB chunks
                std::size_t totalChunks = (fileData.size() + chunkSize - 1) / chunkSize;

                SPDLOG_INFO("Starting file transfer: {} in {} chunks", transfer->fileName, totalChunks);

                // Send file in chunks
                for (std::size_t i = 0; i < totalChunks; ++i) {
                    // Check if transfer has been canceled
                    auto updatedTransfer = findTransfer(transfer->id);
                    if (!updatedTransfer ||
                        updatedTransfer->status == TransferStatus::Canceled ||
                        updatedTransfer->status == TransferStatus::Failed) {
                        SPDLOG_INFO("Transfer aborted during file send: {}", transfer->id);
                        return;
                    }

                    // Calculate chunk bounds
                    std::size_t startPos = i * chunkSize;
                    std::size_t endPos = std::min(startPos + chunkSize, fileData.size());
                    std::size_t chunkBytes = endPos - startPos;

                    // Create a data chunk
                    std::vector<uint8_t> chunkData(fileData.begin() + startPos, fileData.begin() + endPos);

                    // Create file data message
                    network::FileDataMessage dataMsg;
                    dataMsg.transferId = transfer->id;
                    dataMsg.chunkIndex = i;
                    dataMsg.totalChunks = totalChunks;
                    dataMsg.data = std::move(chunkData);

                    // Serialize and send the message
                    auto msgData = network::Protocol::serialize(dataMsg);
                    auto sendFuture = m_socketHandler->sendTcp(endpoint, msgData);
                    int result = sendFuture.get();

                    if (result < 0) {
                        SPDLOG_ERROR("Failed to send file chunk {}/{} for transfer {}",
                                     i, totalChunks, transfer->id);
                        updateTransferStatus(transfer->id, TransferStatus::Failed,
                                             "Failed to send file data");
                        return;
                    }

                    // Update progress - 50% (file read) + 50% (file send progress)
                    std::uintmax_t totalProgress = transfer->fileSize / 2 +
                                                   (transfer->fileSize / 2) * (i + 1) / totalChunks;
                    updateTransferProgress(transfer->id, totalProgress);

                    // Add a small delay to avoid overwhelming the network
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }

                // Send transfer complete message
                network::TransferCompleteMessage completeMsg;
                completeMsg.transferId = transfer->id;
                completeMsg.success = true;
                completeMsg.fileHash = ""; // TODO: Implement file hashing

                // Serialize and send the message
                auto completeData = network::Protocol::serialize(completeMsg);
                auto completeFuture = m_socketHandler->sendTcp(endpoint, completeData);
                int completeResult = completeFuture.get();

                if (completeResult < 0) {
                    SPDLOG_ERROR("Failed to send transfer complete message for {}", transfer->id);
                    updateTransferStatus(transfer->id, TransferStatus::Failed,
                                         "Failed to send transfer complete message");
                    return;
                }

                // Update transfer status
                updateTransferProgress(transfer->id, transfer->fileSize);
                updateTransferStatus(transfer->id, TransferStatus::Completed);

                SPDLOG_INFO("Transfer completed: {}", transfer->id);
            } catch (const std::exception &e) {
                SPDLOG_ERROR("Error during file transfer {}: {}", transfer->id, e.what());
                updateTransferStatus(transfer->id, TransferStatus::Failed,
                                     std::string("Error during transfer: ") + e.what());
            }
        });

        // Detach the thread so it runs independently
        transferThread.detach();
    }

    std::shared_ptr<TransferInfo> TransferManager::findTransferByEndpoint(const std::string &endpoint) {
        std::lock_guard<std::mutex> lock(m_transfersMutex);

        for (const auto &[id, transfer]: m_transfers) {
            if (transfer->peerAddress == endpoint) {
                return transfer;
            }
        }

        return nullptr;
    }

    bool TransferManager::connectToPeer(const PeerInfo &peer) {
        auto endpointStr = peer.ipAddress + ":" + std::to_string(peer.port);

        SPDLOG_INFO("Connecting to peer: {} ({}) at {}", peer.name, peer.id, endpointStr);

        // Check if we're already connected
        {
            std::lock_guard<std::mutex> lock(m_transfersMutex);
            for (const auto &[id, transfer]: m_transfers) {
                if (transfer->peerAddress == endpointStr) {
                    return true; // Already connected
                }
            }
        }

        // Connect to the peer
        bool success = m_socketHandler->connectTcp(
                peer.ipAddress,
                peer.port,
                [this](const std::vector<uint8_t> &data, const std::string &endpoint) {
                    this->handleIncomingData(data, endpoint);
                },
                [this](network::ConnectionStatus status, const std::string &endpoint, const std::string &errorMessage) {
                    this->handleConnectionStatus(status, endpoint, errorMessage);
                }
        );

        return success;
    }

    std::shared_ptr<PeerInfo> TransferManager::getPeerInfo(const std::string &peerId) const {
        // Get all known peers
        std::vector<PeerInfo> peers = m_discoveryService->getKnownPeers();

        // Find the peer with matching id
        for (const auto &peer: peers) {
            if (peer.id == peerId) {
                return std::make_shared<PeerInfo>(peer);
            }
        }

        return nullptr;
    }

    std::string TransferManager::generateTransferId() {
        // Generate a unique ID based on timestamp and counter
        auto now = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
        int id = m_nextTransferId.fetch_add(1);

        std::stringstream ss;
        ss << std::hex << now << "-" << id;
        return ss.str();
    }

    void TransferManager::updateTransferProgress(const std::string &transferId, std::uintmax_t bytesTransferred) {
        auto transfer = findTransfer(transferId);
        if (!transfer) {
            SPDLOG_ERROR("Failed to update the progress: transfer not found: {}", transferId);
            return;
        }

        // Update the transfer of the progress
        transfer->bytesTransferred = bytesTransferred;

        // Calculate progress percentage
        if (transfer->fileSize > 0) {
            transfer->progress = static_cast<float>(bytesTransferred) / static_cast<float>(transfer->fileSize) * 100.0f;
        } else {
            transfer->progress = 100.0f; // Avoid division by zero
        }

        // Notify the callback
        if (m_statusCallback) {
            m_statusCallback(*transfer);
        }

        SPDLOG_DEBUG("Transfer progress updated: {} - {}%", transferId, transfer->progress);
    }

    void TransferManager::updateTransferStatus(const std::string &transferId, TransferStatus status,
                                               const std::string &errorMessage) {
        auto transfer = findTransfer(transferId);
        if (!transfer) {
            SPDLOG_ERROR("Failed to update status: transfer not found: {}", transferId);
            return;
        }

        // Update the transfer status
        transfer->status = status;

        // Set the error message if provided
        if (!errorMessage.empty()) {
            transfer->errorMessage = errorMessage;
        }

        // Record the end time for complete, failed or canceled transfers
        if (status == TransferStatus::Completed ||
            status == TransferStatus::Failed ||
            status == TransferStatus::Canceled) {
            transfer->endTime = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
        }

        // Notify the callback
        if (m_statusCallback) {
            m_statusCallback(*transfer);
        }

        SPDLOG_INFO("Transfer status update: {} - {}", transferId, static_cast<int>(status));

    }

    std::shared_ptr<TransferInfo> TransferManager::findTransfer(const std::string &transferId) {
        std::lock_guard<std::mutex> lock(m_transfersMutex);

        if (auto it = m_transfers.find(transferId); it != m_transfers.end()) {
            return it->second;
        }

        return nullptr;
    }

    void
    TransferManager::processTransferCancel(const network::TransferCancelMessage &cancel, const std::string &endpoint) {
        auto transfer = findTransfer(cancel.transferId);

        if (!transfer) {
            SPDLOG_ERROR("Received cancel for unknown transfer: {}", cancel.transferId);
            return;
        }

        SPDLOG_INFO("Transfer canceled by peer: {} - {}", transfer->id, cancel.reason);

        // Update transfer status
        updateTransferStatus(cancel.transferId, TransferStatus::Canceled,
                             "Canceled by peer: " + cancel.reason);
    }

    void TransferManager::processTransferComplete(const network::TransferCompleteMessage &complete,
                                                  const std::string &endpoint) {
        auto transfer = findTransfer(complete.transferId);

        if (!transfer) {
            SPDLOG_ERROR("Received completion for unknown transfer: {}", complete.transferId);
            return;
        }

        if (complete.success) {
            SPDLOG_INFO("Transfer complete successfully: {}", transfer->id);

            // For incoming transfers, we should have the complete file now
            if (transfer->direction == TransferDirection::Incoming) {
                // TODO: Verify file hash if implemented

                // Update transfer as completed
                updateTransferProgress(complete.transferId, transfer->fileSize);
                updateTransferStatus(complete.transferId, TransferStatus::Completed);
            } else {
                // For outgoing transfers, this is a confirmation from the receiver
                updateTransferStatus(complete.transferId, TransferStatus::Completed);
            }
        } else {
            SPDLOG_ERROR("Transfer failed: {}", transfer->id);
            updateTransferStatus(complete.transferId, TransferStatus::Failed,
                                 "Transfer failed on the remote side");
        }
    }

    void TransferManager::processFileData(const network::FileDataMessage &fileData, const std::string &endpoint) {
        auto transfer = findTransfer(fileData.transferId);

        if (!transfer) {
            SPDLOG_ERROR("Received file data for unknown transfer: {}", fileData.transferId);
            return;
        }

        // Ensure this is an incoming transfer
        if (transfer->direction != TransferDirection::Incoming) {
            SPDLOG_ERROR("Received file data for an outgoing transfer: {}", fileData.transferId);
            return;
        }

        SPDLOG_DEBUG("Received file data chunk {}/{} for transfer {}",
                     fileData.chunkIndex, fileData.totalChunks, fileData.transferId);

        try {
            // Check if this is the first chunk
            if (fileData.chunkIndex == 0) {
                // Create the download directory if it doesn't exist
                auto dirPath = fs::path(m_downloadDirectory);
                if (!fs::exists(dirPath)) {
                    fs::create_directories(dirPath);
                }

                // Set the file path if it wasn't set yet
                if (transfer->filePath.empty()) {
                    transfer->filePath = (dirPath / m_fileHandler->getUniqueFilename(
                            m_downloadDirectory, transfer->fileName)).string();
                    SPDLOG_INFO("File will be saved to: {}", transfer->filePath);
                }

                // Update status to InProgress
                updateTransferStatus(fileData.transferId, TransferStatus::InProgress);

                // Initialize the temporary buffer for collecting file chunks
                {
                    std::lock_guard<std::mutex> lock(m_transferDataMutex);
                    m_transferData[transfer->id] = std::vector<std::vector<uint8_t>>(fileData.totalChunks);
                    m_transferData[transfer->id][0] = fileData.data;
                    m_transferChunksReceived[transfer->id] = 1;
                }
            } else {
                // Store the chunk in the buffer
                {
                    std::lock_guard<std::mutex> lock(m_transferDataMutex);
                    if (m_transferData.find(transfer->id) == m_transferData.end() ||
                        fileData.chunkIndex >= m_transferData[transfer->id].size()) {
                        throw std::runtime_error("Invalid chunk index or transfer data not initialized");
                    }

                    m_transferData[transfer->id][fileData.chunkIndex] = fileData.data;
                    m_transferChunksReceived[transfer->id]++;
                }
            }

            // Calculate current progress based on chunks received
            int chunksReceived = 0;
            int totalChunks = 0;
            {
                std::lock_guard<std::mutex> lock(m_transferDataMutex);
                if (m_transferChunksReceived.find(transfer->id) != m_transferChunksReceived.end()) {
                    chunksReceived = m_transferChunksReceived[transfer->id];
                }
                if (m_transferData.find(transfer->id) != m_transferData.end()) {
                    totalChunks = m_transferData[transfer->id].size();
                }
            }

            // Update progress
            std::uintmax_t bytesTransferred = 0;
            if (totalChunks > 0) {
                bytesTransferred = static_cast<std::uintmax_t>(
                        (static_cast<double>(chunksReceived) / totalChunks) * transfer->fileSize);
            }
            updateTransferProgress(fileData.transferId, bytesTransferred);
            // Check if all chunks have been received
            if (chunksReceived == totalChunks) {
                SPDLOG_INFO("All chunks received for transfer {}, reassembling file", fileData.transferId);

                // Reassemble the file from chunks
                std::vector<uint8_t> completeData;
                {
                    std::lock_guard<std::mutex> lock(m_transferDataMutex);
                    // Calculate total size
                    size_t totalSize = 0;
                    for (const auto &chunk: m_transferData[transfer->id]) {
                        totalSize += chunk.size();
                    }

                    // Allocate space and copy all chunks
                    completeData.reserve(totalSize);
                    for (const auto &chunk: m_transferData[transfer->id]) {
                        completeData.insert(completeData.end(), chunk.begin(), chunk.end());
                    }

                    // Clear the temporary data
                    m_transferData.erase(transfer->id);
                    m_transferChunksReceived.erase(transfer->id);
                }

                // Decrypt the data if encryption is enabled
#ifdef ENABLE_ENCRYPTION
                if (m_encryptionEnabled && !m_encryptionPassword.empty()) {
                    SPDLOG_INFO("Decrypting file data for transfer: {}", transfer->id);
                    std::vector<uint8_t> decryptedData;
                    if (utils::Encryption::decrypt(completeData, m_encryptionPassword, decryptedData)) {
                        SPDLOG_INFO("File data decrypted successfully: {} -> {} bytes",
                                    completeData.size(), decryptedData.size());
                        completeData = std::move(decryptedData);
                    } else {
                        SPDLOG_ERROR("Failed to decrypt file data, saving as is");
                    }
                }
#endif

                // Write the complete file
                if (!m_fileHandler->writeFile(transfer->filePath, completeData)) {
                    throw std::runtime_error("Failed to write file: " + transfer->filePath);
                }

                // Verify the file hash if provided
                std::string receivedHash;
                bool hashVerified = false;

                // Send transfer complete message
                network::TransferCompleteMessage complete;
                complete.transferId = fileData.transferId;
                complete.success = true;

#ifdef ENABLE_ENCRYPTION
                // Calculate and set the file hash
                std::string fileHash = utils::Encryption::calculateFileHash(transfer->filePath);
                complete.fileHash = fileHash;
#endif

                // Serialize and send the message
                auto data = network::Protocol::serialize(complete);
                auto sendFuture = m_socketHandler->sendTcp(endpoint, data);
                int result = sendFuture.get();

                if (result < 0) {
                    SPDLOG_ERROR("Failed to send transfer complete message for {}", fileData.transferId);
                    updateTransferStatus(fileData.transferId, TransferStatus::Failed,
                                         "Failed to send completion acknowledgment");
                    return;
                }

                // Update transfer status to completed
                updateTransferProgress(fileData.transferId, transfer->fileSize);
                updateTransferStatus(fileData.transferId, TransferStatus::Completed);

                SPDLOG_INFO("Transfer completed successfully: {}", fileData.transferId);
            }

        } catch (const std::exception &e) {
            SPDLOG_ERROR("Error processing file data for transfer {}: {}",
                         fileData.transferId, e.what());

            // Update transfer status to failed
            updateTransferStatus(fileData.transferId, TransferStatus::Failed,
                                 std::string("Error processing file data: ") + e.what());

            // Send cancel message to the sender
            network::TransferCancelMessage cancel;
            cancel.transferId = fileData.transferId;
            cancel.reason = "Failed to process file data: " + std::string(e.what());

            // Serialize and send the message
            auto data = network::Protocol::serialize(cancel);
            m_socketHandler->sendTcp(endpoint, data);

            // Clean up any temporary data
            {
                std::lock_guard<std::mutex> lock(m_transferDataMutex);
                m_transferData.erase(transfer->id);
                m_transferChunksReceived.erase(transfer->id);
            }
        }
    }

    // Methods for enabling/disabling encryption
    void TransferManager::setEncryptionEnabled(bool enabled) {
#ifdef ENABLE_ENCRYPTION
        m_encryptionEnabled = enabled;
        SPDLOG_INFO("Encryption {} for file transfers", enabled ? "enabled" : "disabled");
#else
        SPDLOG_WARN("Encryption support not compiled in, ignoring setEncryptionEnabled");
#endif
    }

    bool TransferManager::isEncryptionEnabled() const {
#ifdef ENABLE_ENCRYPTION
        return m_encryptionEnabled;
#else
        return false;
#endif
    }

    void TransferManager::setEncryptionPassword(const std::string& password) {
#ifdef ENABLE_ENCRYPTION
        m_encryptionPassword = password;
        SPDLOG_INFO("Encryption password set");
#else
        SPDLOG_WARN("Encryption support not compiled in, ignoring setEncryptionPassword");
#endif
    }


}
