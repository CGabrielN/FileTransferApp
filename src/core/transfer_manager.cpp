#include "transfer_manager.hpp"
#include "../utils/logging.hpp"

#include <spdlog/spdlog.h>
#include <chrono>
#include <random>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <nlohmann/json.hpp>

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
        }
        else {
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

        // TODO: Implement actual file transfer
        // This would involve reading the file in chunks and sending FileDataMessage
        // messages for each chunk. For now, we'll just simulate a successful transfer.

        // Simulate file transfer completion
        auto completeMessage = network::TransferCompleteMessage();
        completeMessage.transferId = response.transferId;
        completeMessage.success = true;
        completeMessage.fileHash = ""; // TODO: Implement file hashing

        // Serialize and send the message
        auto data = network::Protocol::serialize(completeMessage);

        auto sendFuture = m_socketHandler->sendTcp(endpoint, data);
        int result = sendFuture.get();

        if (result < 0) {
            SPDLOG_ERROR("Failed to send transfer complete message to {}", endpoint);
            updateTransferStatus(response.transferId, TransferStatus::Failed, "Failed to send transfer complete message");
            return;
        }

        // Update transfer status
        updateTransferProgress(response.transferId, transfer->fileSize);
        updateTransferStatus(response.transferId, TransferStatus::Completed);

        SPDLOG_INFO("Transfer completed: {}", response.transferId);
    }

    std::shared_ptr<TransferInfo> TransferManager::findTransferByEndpoint(const std::string &endpoint) {
        return std::shared_ptr<TransferInfo>();
    }

    bool TransferManager::connectToPeer(const PeerInfo &peer) {
        return false;
    }

    std::shared_ptr<PeerInfo> TransferManager::getPeerInfo(const std::string &peerId) const {
        return std::shared_ptr<PeerInfo>();
    }

    std::string TransferManager::generateTransferId() {
        return std::string();
    }

    void TransferManager::updateTransferProgress(const std::string &transferId, std::uintmax_t bytesTransfered) {

    }

    void TransferManager::updateTransferStatus(const std::string &transferId, TransferStatus status,
                                               const std::string &errorMessage) {

    }

    std::shared_ptr<TransferInfo> TransferManager::findTransfer(const std::string &transferId) {
        return std::shared_ptr<TransferInfo>();
    }

    void
    TransferManager::processTransferCancel(const network::TransferCancelMessage &cancel, const std::string &endpoint) {

    }

    void TransferManager::processTransferComplete(const network::TransferCompleteMessage &complete,
                                                  const std::string &endpoint) {

    }

    void TransferManager::processFileData(const network::FileDataMessage &fileData, const std::string &endpoint) {

    }


}
