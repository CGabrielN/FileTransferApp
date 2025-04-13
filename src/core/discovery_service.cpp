#include "discovery_service.hpp"
#include "../utils/logging.hpp"

#include <spdlog/spdlog.h>
#include <chrono>
#include <random>
#include <iomanip>
#include <sstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using namespace std::chrono;

namespace core {

    // PeerInfo serialization/deserialization
    json PeerInfo::toJson() const {
        return {
                {"id",        id},
                {"name",      name},
                {"ipAddress", ipAddress},
                {"port",      port},
                {"platform",  platform},
                {"version",   version},
                {"lastSeen",  lastSeen}
        };
    }

    PeerInfo PeerInfo::fromJson(const nlohmann::json &j) {
        PeerInfo info;
        info.id = j["id"].get<std::string>();
        info.name = j["name"].get<std::string>();
        info.ipAddress = j["ipAddress"].get<std::string>();
        info.port = j["port"].get<uint16_t>();
        info.platform = j["platform"].get<std::string>();
        info.version = j["version"].get<std::string>();
        info.lastSeen = j["lastSeen"].get<int64_t>();
        return info;
    }

    DiscoveryService::DiscoveryService(std::shared_ptr<network::SocketHandler> socketHandler,
                                       std::shared_ptr<platform::Platform> platform, uint16_t discoveryPort,
                                       uint32_t announcementInterval, uint32_t timeoutInterval)
            : m_socketHandler(std::move(socketHandler)), m_platform(std::move(platform)),
              m_discoveryPort(discoveryPort), m_announcementInterval(announcementInterval),
              m_timeoutInterval(timeoutInterval), m_peerId(generatePeerId()),
              m_displayName("User on " + m_platform->getName()), m_running(false) {

        if (discoveryPort == 34567) {
            // Use a random port between 40000 and 49999
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dist(40000, 49999);
            m_discoveryPort = dist(gen);
        }
        SPDLOG_DEBUG("DiscoveryService initialized with peer ID: {}", m_peerId);
    }

    DiscoveryService::~DiscoveryService() {
        stop();
    }

    void DiscoveryService::start() {
        if (m_running.exchange(true)) {
            SPDLOG_WARN("DiscoveryService already running");
            return;
        }

        SPDLOG_INFO("Starting DiscoveryService");

        // Initialize UDP socket for discovery
        bool success = m_socketHandler->initUdpSocket(m_discoveryPort,
                                                      [this](const std::vector<uint8_t> &data,
                                                             const std::string &endpoint) {
                                                          this->handleDiscoveryMessage(data, endpoint);
                                                      }
        );

        if (!success) {
            SPDLOG_ERROR("Failed to initialize UDP socket for discovery");
            m_running = false;
            return;
        }

        // Start announcement thread
        m_announceThread = std::thread([this]() {
            SPDLOG_DEBUG("Announcement thread started");

            while (m_running) {
                try {
                    sendAnnouncement();

                    // Sleep for the announcement interval
                    std::this_thread::sleep_for(std::chrono::milliseconds(m_announcementInterval));
                } catch (const std::exception &e) {
                    SPDLOG_ERROR("Exception in announcement thread: {}", e.what());
                }
            }

            SPDLOG_DEBUG("Announcement thread stopped");
        });

        // Start timeout check thread
        m_timeoutThread = std::thread([this]() {
            SPDLOG_DEBUG("Timeout check thread started");

            while (m_running) {
                try {
                    checkPeerTimeouts();

                    // Sleep for 1 second
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                } catch (const std::exception &e) {
                    SPDLOG_ERROR("Exception in timeout check thread: {}", e.what());
                }
            }

            SPDLOG_DEBUG("Timeout check thread stopped");
        });

        m_running = true;

        SPDLOG_INFO("DiscoveryService started");
    }

    void DiscoveryService::stop() {
        if (!m_running.exchange(false)) {
            return; // Already stopped
        }

        SPDLOG_INFO("Stopping DiscoveryService");

        // wait for threads to finish
        if (m_announceThread.joinable()) {
            m_announceThread.join();
        }

        if (m_timeoutThread.joinable()) {
            m_timeoutThread.join();
        }

        SPDLOG_INFO("DiscoveryService stopped");
    }

    bool DiscoveryService::isRunning() const {
        return m_running;
    }

    void DiscoveryService::setDisplayName(const std::string &name) {
        m_displayName = name;
        SPDLOG_DEBUG("Display name set to: {}", m_displayName);

        if (m_running) {
            sendAnnouncement();
        }
    }

    std::string DiscoveryService::getDisplayName() const {
        return m_displayName;
    }

    std::string DiscoveryService::getPeerId() const {
        return m_peerId;
    }

    std::vector<PeerInfo> DiscoveryService::getKnownPeers() const {
        std::lock_guard<std::mutex> lock(m_peersMutex);

        std::vector<PeerInfo> result;
        result.reserve(m_peers.size());

        for (const auto &[id, peer]: m_peers) {
            result.push_back(peer);
        }

        return result;
    }

    void DiscoveryService::registerPeerDiscoveryCallback(PeerDiscoveredCallback callback) {
        m_peerDiscoveredCallback = std::move(callback);
    }

    void DiscoveryService::registerPeerLostCallback(PeerLostCallback callback) {
        m_peerLostCallback = std::move(callback);
    }

    void DiscoveryService::handleDiscoveryMessage(const std::vector<uint8_t> &data, const std::string &endpoint) {
        try {
            // Parse the JSON message
            std::string message(data.begin(), data.end());
            json j = json::parse(message);

            // Extract the announcement type
            std::string type = j["type"].get<std::string>();

            if (type == "announcement") {
                // Extract peer information
                std::string peerId = j["peerId"].get<std::string>();

                // Ignore our own announcements
                if (peerId == m_peerId) {
                    return;
                }

                // Extract the IP address from the endpoint
                size_t colonPos = endpoint.find(":");
                std::string ipAddress = endpoint.substr(0, colonPos);

                // Check if this is a new peer or an update
                bool isNew = false;

                PeerInfo peer;
                peer.id = peerId;
                peer.name = j["name"].get<std::string>();
                peer.ipAddress = ipAddress;
                peer.port = j["port"].get<uint16_t>();
                peer.platform = j["platform"].get<std::string>();
                peer.version = j["version"].get<std::string>();
                peer.lastSeen = duration_cast<milliseconds>(
                        system_clock::now().time_since_epoch()).count();

                {
                    std::lock_guard<std::mutex> lock(m_peersMutex);

                    auto it = m_peers.find(peerId);
                    if (it == m_peers.end()) {
                        isNew = true;
                        SPDLOG_INFO("New peer discovered: {} ({}) at {}:{}",
                                    peer.name, peer.id, peer.ipAddress, peer.port);
                    } else {
                        SPDLOG_DEBUG("Peer updated: {} ({}) at {}:{}",
                                     peer.name, peer.id, peer.ipAddress, peer.port);
                    }

                    // Update or insert the peer
                    m_peers[peerId] = peer;
                }

                // Notify callback
                if (m_peerDiscoveredCallback) {
                    m_peerDiscoveredCallback(peer, isNew);
                }
            }
        } catch (const std::exception &e) {
            SPDLOG_ERROR("Error handling discovery message: {}", e.what());
        }
    }

    void DiscoveryService::sendAnnouncement() {
        if (!m_running) {
            return;
        }

        try {
            // Create the announcement message
            json announcement = {
                    {"type",      "announcement"},
                    {"peerId",    m_peerId},
                    {"name",      m_displayName},
                    {"port",      m_discoveryPort},  // Using the same port for discovery and connections
                    {"platform",  m_platform->getName()},
                    {"version",   "1.0.0"},  // Should be taken from a global version constant
                    {"timestamp", duration_cast<milliseconds>(
                            system_clock::now().time_since_epoch()).count()}
            };

            std::string message = announcement.dump();
            std::vector<uint8_t> data(message.begin(), message.end());

            // Broadcast the announcement
            int result = m_socketHandler->sendUdpBroadcast(m_discoveryPort, data);

            if (result < 0) {
                SPDLOG_ERROR("Failed to send discovery announcement");
            } else {
                SPDLOG_DEBUG("Sent discovery announcement ({} bytes)", result);
            }
        } catch (const std::exception &e) {
            SPDLOG_ERROR("Error sending announcement: {}", e.what());
        }
    }


    void DiscoveryService::checkPeerTimeouts() {
        if (!m_running) {
            return;
        }

        int64_t now = duration_cast<milliseconds>(
                system_clock::now().time_since_epoch()).count();

        std::vector<std::string> lostPeers;

        {
            std::lock_guard<std::mutex> lock(m_peersMutex);

            // Check each peer
            for (auto it = m_peers.begin(); it != m_peers.end();) {
                const auto &peer = it->second;

                // Check if the peer has timed out
                if (now - peer.lastSeen > m_timeoutInterval) {
                    SPDLOG_INFO("Peer lost: {} ({}) at {}:{}",
                                peer.name, peer.id, peer.ipAddress, peer.port);
                    lostPeers.push_back(peer.id);
                    it = m_peers.erase(it);
                } else {
                    ++it;
                }
            }
        }

        // Notify callbacks for lost peers
        if (m_peerLostCallback) {
            for (const auto &peerId: lostPeers) {
                m_peerLostCallback(peerId);
            }
        }
    }

    std::string DiscoveryService::generatePeerId() const {
        // Generate a random UUID-like string
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 15);

        std::stringstream ss;
        ss << std::hex << std::setfill('0');

        for (int i = 0; i < 8; ++i) {
            ss << std::setw(2) << dis(gen);
        }
        ss << "-";

        for (int i = 0; i < 4; ++i) {
            ss << std::setw(2) << dis(gen);
        }
        ss << "-4";  // Version 4 UUID

        for (int i = 0; i < 3; ++i) {
            ss << std::setw(2) << dis(gen);
        }
        ss << "-";

        // 8, 9, A, or B for the variant
        ss << std::setw(1) << (dis(gen) & 0x3 + 8);

        for (int i = 0; i < 3; ++i) {
            ss << std::setw(2) << dis(gen);
        }
        ss << "-";

        for (int i = 0; i < 6; ++i) {
            ss << std::setw(2) << dis(gen);
        }

        return ss.str();
    }


}