#pragma once

#include "file_handler.hpp"
#include "discovery_service.hpp"
#include "../network/socket_handler.hpp"
#include "../network/protocol.hpp"

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <map>
#include <mutex>
#include <atomic>
#include <future>

namespace core{

    /**
     * Transfer status enum
     */
    enum class TransferStatus{
        Initializing,
        Waiting,
        InProgress,
        Completed,
        Failed,
        Canceled
    };

    /**
     * Direction of the transfer
     */
    enum class TransferDirection{
        Incoming,
        Outgoing
    };

    /**
     * Information about a file transfer
     */
    struct TransferInfo{
        std::string id;                  // Unique transfer ID
        std::string peerId;              // ID of the peer
        std::string peerName;            // Name of the peer
        std::string peerAddress;         // Address of the peer
        TransferDirection direction;     // Transfer direction
        TransferStatus status;           // Current status
        std::string filePath;            // Path to the file
        std::string fileName;            // Name of the file
        std::uintmax_t fileSize;         // Size of the file in bytes
        std::uintmax_t bytesTransferred; // Number of bytes transferred
        float progress;                  // Progress percentage (0-100)
        int64_t startTime;               // Timestamp when the transfer started
        int64_t endTime;                 // Timestamp when the transfer completed/failed
        std::string errorMessage;        // Error message if the transfer failed

        // For serialization/deserialization
        nlohmann::json toJson() const;
        static TransferInfo fromJson(const nlohmann::json& j);
    };

    /**
     * Callback for transfer status updates
     * @param transfer The updated transfer information
     */
    using TransferStatusCallback = std::function<void(const TransferInfo& transfer)>;

    /**
     * Callback for transfer request notifications
     * @param transfer The transfer information
     * @return True to accept the transfer, false to reject it
     */
    using TransferRequestCallback = std::function<bool(const TransferInfo& transfer)>;


    /**
     * Manages file transfers between peers
     */
    class TransferManager{
    private:
        std::shared_ptr<FileHandler> m_fileHandler;
        std::shared_ptr<network::SocketHandler> m_socketHandler;
        std::shared_ptr<DiscoveryService> m_discoveryService;
        uint16_t m_serverPort;

        std::string m_downloadDirectory;
        std::atomic<bool> m_initialized;
        std::atomic<int> m_nextTransferId;

        mutable std::mutex m_transfersMutex;
        std::map<std::string, std::shared_ptr<TransferInfo>> m_transfers;

        // Store file data during transfers
        mutable std::mutex m_transferDataMutex;
        std::unordered_map<std::string, std::vector<std::vector<uint8_t>>> m_transferData;
        std::unordered_map<std::string, int> m_transferChunksReceived;

        // Encryption settings
#ifdef ENABLE_ENCRYPTION
        bool m_encryptionEnabled = false;
        std::string m_encryptionPassword;
#endif

        TransferStatusCallback m_statusCallback;
        TransferRequestCallback m_requestCallback;


        /**
         * Handle incoming data from a peer
         * @param data The received data
         * @param endpoint The sender's endpoint
         */
        void handleIncomingData(const std::vector<uint8_t>&data, const std::string& endpoint);

        /**
         * Handle connection status changes
         * @param status The new connection status
         * @param endpoint The endpoint that changed
         * @param errorMessage Error message if any
         */
        void handleConnectionStatus(network::ConnectionStatus status,
                                    const std::string& endpoint,
                                    const std::string& errorMessage);

        /**
         * Process a transfer request
         * @param request The transfer request message
         * @param endpoint The sender's endpoint
         */
        void processTransferRequest(const network::TransferRequestMessage& request,
                                    const std::string& endpoint);

        /**
         * Process a transfer response
         * @param response The transfer response message
         * @param endpoint The sender's endpoint
         */
        void processTransferResponse(const network::TransferResponseMessage& response,
                                     const std::string& endpoint);

        /**
         * Process file data
         * @param fileData The file data message
         * @param endpoint The sender's endpoint
         */
        void processFileData(const network::FileDataMessage& fileData,
                             const std::string& endpoint);

        /**
         * Process a transfer complete notification
         * @param complete The transfer complete message
         * @param endpoint The sender's endpoint
         */
        void processTransferComplete(const network::TransferCompleteMessage& complete,
                                     const std::string& endpoint);

        /**
         * Process a transfer cancel notification
         * @param cancel The transfer cancel message
         * @param endpoint The sender's endpoint
         */
        void processTransferCancel(const network::TransferCancelMessage& cancel,
                                   const std::string& endpoint);

        /**
         * Find a transfer by its ID
         * @param transferId The transfer ID to find
         * @return Shared pointer to the transfer info or nullptr if not found
         */
        std::shared_ptr<TransferInfo> findTransfer(const std::string& transferId);

        /**
         * Update a transfer's status and notify the callback
         * @param transferId The ID of the transfer to update
         * @param status The new status
         * @param errorMessage Optional error message
         */
        void updateTransferStatus(const std::string& transferId, TransferStatus status,
                                  const std::string& errorMessage = "");

        /**
         * Update a transfer's progress and notify the callback
         * @param transferId The ID of the transfer to update
         * @param bytesTransferred The number of bytes transferred
         */
        void updateTransferProgress(const std::string& transferId,
                                    std::uintmax_t bytesTransferred);

        /**
         * Generate a unique transfer ID
         * @return A unique ID string
         */
        std::string generateTransferId();

        /**
         * Get the peer information for a given peer ID
         * @param peerId The peer ID to find
         * @return The peer information or nullptr if not found
         */
        std::shared_ptr<PeerInfo> getPeerInfo(const std::string& peerId) const;

        /**
         * Connect to a peer for file transfer
         * @param peer The peer information
         * @return True if the connection was established, false otherwise
         */
        bool connectToPeer(const PeerInfo& peer);

        /**
         * Find a transfer by peer endpoint
         * @param endpoint The peer endpoint
         * @return Shared pointer to the transfer info or nullptr if not found
         */
        std::shared_ptr<TransferInfo> findTransferByEndpoint(const std::string& endpoint);


    public:
        /**
         * Constructor
         * @param fileHandler File handler for file operations
         * @param socketHandler Socket handler for network communication
         * @param discoveryService Discovery service for peer discovery
         * @param serverPort Port to listen on for incoming transfers (default: 34568)
         */
        TransferManager(std::shared_ptr<FileHandler> fileHandler,
                        std::shared_ptr<network::SocketHandler> socketHandler,
                        std::shared_ptr<DiscoveryService> discoveryService,
                        uint16_t serverPort = 34568);

        /**
         * Destructor
         */
        ~TransferManager();

        /**
         * Initialize the transfer manager
         * @return True if initialization was successful, false otherwise
         */
        bool init();

        /**
         * Shutdown the transfer manager
         */
        void shutdown();

        /**
         * Send a file to a peer
         * @param peerId ID of the peer to send to
         * @param filePath Path to the file to send
         * @return Transfer ID if the transfer was initiated, empty string otherwise
         */
        std::string sendFile(const std::string& peerId, const std::string& filePath);

        /**
         * Cancel a transfer
         * @param transferId ID of the transfer to cancel
         * @return True if the transfer was canceled, false otherwise
         */
        bool cancelTransfer(const std::string& transferId);

        /**
         * Get information about a specific transfer
         * @param transferId ID of the transfer
         * @return Transfer information or nullptr if not found
         */
        std::shared_ptr<TransferInfo> getTransferInfo(const std::string& transferId) const;

        /**
         * Get a list of all transfers
         * @return Vector of transfer information
         */
        std::vector<TransferInfo> getAllTransfers() const;

        /**
         * Register a callback for transfer status updates
         * @param callback The callback function
         */
        void registerStatusCallback(TransferStatusCallback callback);

        /**
         * Register a callback for transfer request notifications
         * @param callback The callback function
         */
        void registerRequestCallback(TransferRequestCallback callback);

        /**
         * Get the default download directory
         * @return Path to the default download directory
         */
        std::string getDefaultDownloadDirectory() const;

        /**
         * Set the default download directory
         * @param directory Path to the directory
         */
        void setDefaultDownloadDirectory(const std::string& directory);

        /**
         * Enable or disable file encryption
         * @param enabled True to enable encryption, false to disable
         */
        void setEncryptionEnabled(bool enabled);

        /**
         * Check if encryption is enabled
         * @return True if encryption is enabled, false otherwise
         */
        bool isEncryptionEnabled() const;

        /**
         * Set the encryption password
         * @param password Password to use for encryption
         */
        void setEncryptionPassword(const std::string& password);

    };

}