#pragma once

#include "../network/socket_handler.hpp"
#include "../platform/platform.hpp"

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <mutex>
#include <atomic>
#include <thread>
#include <nlohmann/json.hpp>

namespace core {

    /**
     * Represents a discovered peer on the network
     */
    struct PeerInfo {
        std::string id;  // Unique ID of the peer
        std::string name; // Display name of the peer
        std::string ipAddress; // IP address of the peer
        uint16_t port; // Port the peer is listening on
        std::string platform; // Platform the peer is running on
        std::string version; // Application version of the peer
        int64_t lastSeen; // Timestamp when the peer was last seen

        // For serialization/deserialization
        nlohmann::json toJson() const;
        static PeerInfo fromJson(const nlohmann::json& j);
    };

    /**
     * Callback for peer discovery events
     * @param peer The discovered peer information
     * @param isNew True if this is a newly discovered peer, false if it's an update
     */
    using PeerDiscoveredCallback = std::function<void(const PeerInfo& peer, bool isNew)>;

    /**
     * Callback for peer lost events
     * @param peerId ID of the peer that was lost
     */
    using PeerLostCallback = std::function<void(const std::string& peerId)>;

    /**
     * Service for discovering other instances of the application on the local network
     */
    class DiscoveryService{
    private:
        std::shared_ptr<network::SocketHandler> m_socketHandler;
        std::shared_ptr<platform::Platform> m_platform;
        uint16_t m_discoveryPort;
        uint32_t m_announcementInterval;
        uint32_t m_timeoutInterval;

        std::string m_peerId;
        std::string m_displayName;
        std::atomic<bool> m_running;

        std::thread m_announceThread;
        std::thread m_timeoutThread;

        mutable std::mutex m_peersMutex;
        std::unordered_map<std::string, PeerInfo> m_peers;

        PeerDiscoveredCallback m_peerDiscoveredCallback;
        PeerLostCallback m_peerLostCallback;

        /**
         * Handle received discovery messages
         * @param data The received data
         * @param endpoint The sender's endpoint
         */
        void handleDiscoveryMessage(const std::vector<uint8_t>& data, const std::string& endpoint);


        /**
         * Send an announcement of this peer's presence
         */
        void sendAnnouncement();

        /**
         * Check for timed-out peers
         */
        void checkPeerTimeouts();

        /**
         * Generate a unique peer ID
         * @return A unique ID string
         */
        std::string generatePeerId() const;


    public:
        /**
         * Constructor
         * @param socketHandler Socket handler for network communication
         * @param platform Platform-specific implementation
         * @param discoveryPort Port to use for discovery (default: 34567)
         * @param announcementInterval Interval between discovery announcements in ms (default: 5000)
         * @param timeoutInterval Time after which a peer is considered lost in ms (default: 15000)
         */
        DiscoveryService(std::shared_ptr<network::SocketHandler> socketHandler,
                         std::shared_ptr<platform::Platform> platform,
                         uint16_t discoveryPort = 34567,
                         uint32_t announcementInterval = 5000,
                         uint32_t timeoutInterval = 15000);

        /**
         * Destructor
         */
        ~DiscoveryService();

        /**
         * Start the discovery service
         */
        void start();

        /**
         * Stop the discovery service
         */
         void stop();

         /**
          * Check if the discovery service is running
          * @return True if the service is running, false otherwise
          */
         bool isRunning() const;

         /**
          * Set the display name for this peer
          * @param name The name to display to other peers
          */
         void setDisplayName(const std::string& name);

         /**
          * Get the display name for this peer
          * @return The current display name
          */
        std::string getDisplayName() const;

        /**
         * Get this peer's unique ID
         * @return The peer ID
         */
        std::string getPeerId() const;

        /**
         * Get a list of all currently known peers
         * @return Vector of peer information
         */
        std::vector<PeerInfo> getKnownPeers() const;

        /**
         * Register a callback for peer discovery events
         * @param callback The callback function
         */
        void registerPeerDiscoveryCallback(PeerDiscoveredCallback callback);

        /**
         * Register a callback for peer lost events
         * @param callback The callback function
         */
        void registerPeerLostCallback(PeerLostCallback callback);
    };
}
