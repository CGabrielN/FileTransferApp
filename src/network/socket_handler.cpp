#include "socket_handler.hpp"
#include "../utils/logging.hpp"

#include <spdlog/spdlog.h>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include <iostream>

namespace network {
    class SocketHandler::Impl {
    private:
        // ASIO context and thread
        asio::io_context m_ioContext;
        asio::executor_work_guard<asio::io_context::executor_type> m_work;
        std::thread m_ioThread;
        std::atomic<bool> m_running;

        // TCP Server components
        std::unique_ptr<asio::ip::tcp::acceptor> m_tcpAcceptor;

        // UDP socket
        std::unique_ptr<asio::ip::udp::socket> m_udpSocket;

        // Socket storage
        std::mutex m_socketsMutex;
        std::unordered_map<std::string, std::shared_ptr<asio::ip::tcp::socket>> m_tcpSockets;

        // Callbacks
        DataReceivedCallback m_tcpDataCallback;
        ConnectionStatusCallback m_tcpStatusCallback;
        std::unordered_map<std::string, DataReceivedCallback> m_tcpDataCallbacks;
        std::unordered_map<std::string, ConnectionStatusCallback> m_tcpStatusCallbacks;
        DataReceivedCallback m_udpDataCallback;


        void startAcceptingConnections() {
            if (!m_tcpAcceptor || !m_running) {
                return;
            }

            // Create new socket for the incoming connection
            auto newSocket = std::make_shared<asio::ip::tcp::socket>(m_ioContext);

            // Accept a new connection
            m_tcpAcceptor->async_accept(*newSocket,
                                        [this, newSocket](const asio::error_code &error) {
                                            if (!m_running) {
                                                return;
                                            }

                                            if (!error) {
                                                // Get the remote endpoint as string
                                                asio::ip::tcp::endpoint remote = newSocket->remote_endpoint();
                                                std::string endpointStr = remote.address().to_string() + ":" +
                                                                          std::to_string(remote.port());

                                                SPDLOG_INFO("Accepted connection from {}", endpointStr);

                                                // Store the socket
                                                {
                                                    std::lock_guard<std::mutex> lock(m_socketsMutex);
                                                    m_tcpSockets[endpointStr] = newSocket;
                                                }

                                                // Notify the status callback
                                                if (m_tcpStatusCallback) {
                                                    m_tcpStatusCallback(ConnectionStatus::Connected, endpointStr, "");
                                                }

                                                // Start receiving data
                                                startReceive(newSocket, endpointStr);
                                            } else {
                                                SPDLOG_ERROR("Error accepting connection: {}", error.message());
                                            }

                                            // Accept the next connection
                                            startAcceptingConnections();
                                        });
        }

        void startReceive(std::shared_ptr<asio::ip::tcp::socket> socket, const std::string &endpoint) {
            if (!socket->is_open() || !m_running) {
                return;
            }

            // Create a buffer for receiving data
            auto receiveBuffer = std::make_shared<std::vector<uint8_t>>(64 * 1024); // 64KB buffer

            // Receive data asynchronously
            socket->async_read_some(
                    asio::buffer(receiveBuffer->data(), receiveBuffer->size()),
                    [this, socket, endpoint, receiveBuffer](const asio::error_code &error, std::size_t bytesReceived) {
                        if (!m_running) {
                            return;
                        }

                        if (!error) {
                            SPDLOG_DEBUG("Received {} bytes from {}", bytesReceived, endpoint);

                            // Resize the buffer to the actual data size
                            receiveBuffer->resize(bytesReceived);

                            // Call the data callback
                            if (auto it = m_tcpDataCallbacks.find(endpoint); it != m_tcpDataCallbacks.end() &&
                                                                             it->second) {
                                it->second(*receiveBuffer, endpoint);
                            } else if (m_tcpDataCallback) {
                                m_tcpDataCallback(*receiveBuffer, endpoint);
                            }

                            // Continue receiving
                            startReceive(socket, endpoint);
                        } else if (error == asio::error::eof || error == asio::error::connection_reset) {
                            SPDLOG_INFO("Connection closed by peer: {}", endpoint);

                            // Close the socket
                            {
                                std::lock_guard<std::mutex> lock(m_socketsMutex);
                                socket->close();
                                m_tcpSockets.erase(endpoint);
                            }

                            // Notify the status callbacks
                            if (auto it = m_tcpStatusCallbacks.find(endpoint); it != m_tcpStatusCallbacks.end() &&
                                                                               it->second) {
                                it->second(ConnectionStatus::Disconnected, endpoint, "");
                            } else if (m_tcpStatusCallback) {
                                m_tcpStatusCallback(ConnectionStatus::Disconnected, endpoint, "");
                            }
                        } else {
                            SPDLOG_ERROR("Error receiving data from {}: {}", endpoint, error.message());

                            // Close the socket
                            {
                                std::lock_guard<std::mutex> lock(m_socketsMutex);
                                socket->close();
                                m_tcpSockets.erase(endpoint);
                            }

                            // Notify the status callbacks
                            if (auto it = m_tcpStatusCallbacks.find(endpoint); it != m_tcpStatusCallbacks.end() &&
                                                                               it->second) {
                                it->second(ConnectionStatus::Error, endpoint, error.message());
                            } else if (m_tcpStatusCallback) {
                                m_tcpStatusCallback(ConnectionStatus::Error, endpoint, error.message());
                            }
                        }
                    }
            );
        }

        void startUdpReceive() {
            if (!m_udpSocket || !m_udpSocket->is_open() || !m_running) {
                return;
            }

            // Create a buffer for receiving data
            auto receiveBuffer = std::make_shared<std::vector<uint8_t>>(64 * 1024); // 64KB buffer
            auto senderEndpoint = std::make_shared<asio::ip::udp::endpoint>();

            // Receive data asynchronously
            m_udpSocket->async_receive_from(
                    asio::buffer(receiveBuffer->data(), receiveBuffer->size()),
                    *senderEndpoint,
                    [this, receiveBuffer, senderEndpoint](const asio::error_code &error, std::size_t bytesReceived) {
                        if(!m_running){
                            return;
                        }

                        if(!error){
                            std::string endpointStr = senderEndpoint->address().to_string() + ":" +
                                    std::to_string(senderEndpoint->port());

                            SPDLOG_DEBUG("Received {} bytes from UDP endpoint {}", bytesReceived, endpointStr);

                            // Resize the buffer to the actual size
                            receiveBuffer->resize(bytesReceived);

                            // Call the data callback
                            if(m_udpDataCallback){
                                m_udpDataCallback(*receiveBuffer, endpointStr);
                            }
                        } else {
                            SPDLOG_ERROR("Error receiving UDP data: {}", error.message());
                        }

                        // Continue receiving
                        startUdpReceive();
                    }
            );
        }


    public:
        Impl() : m_ioContext(), m_work(asio::make_work_guard(m_ioContext)), m_running(true) {
            // Start the IO service thread
            m_ioThread = std::thread([this]() {
                try {
                    m_ioContext.run();
                    SPDLOG_DEBUG("IO context thread exited");
                } catch (const std::exception &e) {
                    SPDLOG_ERROR("Exception in IO context thread: {}", e.what());
                }
            });

            SPDLOG_DEBUG("SocketHandler initialized");
        }

        ~Impl() {
            shutdown();
        }

        bool initTcpServer(uint16_t port,
                           DataReceivedCallback onDataReceived,
                           ConnectionStatusCallback onConnectionStatus) {
            try {
                SPDLOG_INFO("Starting TCP server on port {}", port);

                m_tcpAcceptor = std::make_unique<asio::ip::tcp::acceptor>(
                        m_ioContext,
                        asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)
                );

                m_tcpDataCallback = std::move(onDataReceived);
                m_tcpStatusCallback = std::move(onConnectionStatus);

                startAcceptingConnections();
                return true;
            } catch (const std::exception &e) {
                SPDLOG_ERROR("Failed to initialize TCP server: {}", e.what());
                return false;
            }
        }

        bool connectTcp(const std::string &host,
                        uint16_t port,
                        DataReceivedCallback onDataReceived,
                        ConnectionStatusCallback onConnectionStatus) {
            try {
                SPDLOG_INFO("Connecting to {}:{}", host, port);

                // Create a new socket
                auto socket = std::make_shared<asio::ip::tcp::socket>(m_ioContext);

                // Resolve the host
                asio::ip::tcp::resolver resolver(m_ioContext);
                auto endpoints = resolver.resolve(host, std::to_string(port));

                // Store the callbacks
                std::string endpointStr = host + ":" + std::to_string(port);
                m_tcpDataCallbacks[endpointStr] = std::move(onDataReceived);
                m_tcpStatusCallbacks[endpointStr] = std::move(onConnectionStatus);

                asio::async_connect(*socket, endpoints,
                                    [this, socket, endpointStr](const asio::error_code &error,
                                                                const asio::ip::tcp::endpoint &endpoint) {
                                        if (!error) {
                                            SPDLOG_INFO("Connected to {}", endpointStr);

                                            //Store the socket
                                            {
                                                std::lock_guard<std::mutex> lock(m_socketsMutex);
                                                m_tcpSockets[endpointStr] = socket;
                                            }

                                            // Notify the status callback
                                            if (auto it = m_tcpStatusCallbacks.find(endpointStr); it !=
                                                                                                  m_tcpStatusCallbacks.end()) {
                                                it->second(ConnectionStatus::Connected, endpointStr, "");
                                            }

                                            // Start receiving data
                                            startReceive(socket, endpointStr);
                                        } else {
                                            SPDLOG_ERROR("Failed to connect to {}: {}", endpointStr, error.message());

                                            // Notify the status callback
                                            if (auto it = m_tcpStatusCallbacks.find(endpointStr); it !=
                                                                                                  m_tcpStatusCallbacks.end()) {
                                                it->second(ConnectionStatus::Error, endpointStr, error.message());
                                            }
                                        }
                                    });

                return true;
            } catch (const std::exception &e) {
                SPDLOG_ERROR("Exception in connectTcp: {}", e.what());
                return false;
            }
        }

        std::future<int> sendTcp(const std::string &endpoint, const std::vector<uint8_t> &data) {
            // Create a promise to return the result
            auto promise = std::make_shared<std::promise<int>>();
            auto future = promise->get_future();

            m_ioContext.post([this, endpoint, data, promise]() {
                try {
                    std::lock_guard<std::mutex> lock(m_socketsMutex);

                    auto it = m_tcpSockets.find(endpoint);
                    if (it == m_tcpSockets.end()) {
                        SPDLOG_ERROR("No connection found for endpoint: {}", endpoint);
                        promise->set_value(-1);
                        return;
                    }

                    auto &socket = it->second;

                    // Check if socket is open
                    if (!socket->is_open()) {
                        SPDLOG_ERROR("Socket for {} is not open", endpoint);
                        promise->set_value(-1);
                        return;
                    }

                    // Send data asynchronously
                    asio::async_write(*socket, asio::buffer(data.data(), data.size()),
                                      [promise, endpoint](const asio::error_code &error, std::size_t bytesSent) {
                                          if (!error) {
                                              SPDLOG_DEBUG("Sent {} bytes to {}", bytesSent, endpoint);
                                              promise->set_value(static_cast<int>(bytesSent));
                                          } else {
                                              SPDLOG_ERROR("Error sending data to {}: {}", endpoint, error.message());
                                              promise->set_value(-1);
                                          }
                                      });
                } catch (const std::exception &e) {
                    SPDLOG_ERROR("Exception in sendTcp: {}", e.what());
                    promise->set_value(-1);
                }
            });

            return future;
        }

        bool initUdpSocket(uint16_t port, DataReceivedCallback onDataReceived) {
            try {
                SPDLOG_INFO("Initializing UDP socket on port {}", port);

                // Create the socket
                m_udpSocket = std::make_unique<asio::ip::udp::socket>(
                        m_ioContext,
                        asio::ip::udp::endpoint(asio::ip::udp::v4(), port)
                );

                // Allow broadcasting
                m_udpSocket->set_option(asio::socket_base::broadcast(true));

                // Store the callback
                m_udpDataCallback = std::move(onDataReceived);

                // Start receiving UDP datagrams
                startUdpReceive();

                return true;
            } catch (const std::exception &e) {
                SPDLOG_ERROR("Failed to initialie UDP socket: {}", e.what());
                return false;
            }
        }


        int sendUdpBroadcast(uint16_t port, const std::vector<uint8_t> &data) {
            try {
                if (!m_udpSocket || !m_udpSocket->is_open()) {
                    SPDLOG_ERROR("UDP socket not initialized");
                    return -1;
                }

                asio::ip::udp::endpoint broadcastEndpoint(
                        asio::ip::address_v4::broadcast(),
                        port
                );

                // Send the data
                auto bytesSent = m_udpSocket->send_to(
                        asio::buffer(data.data(), data.size()),
                        broadcastEndpoint
                );

                SPDLOG_DEBUG("Sent {} bytes as UDP broadcast to port {}", bytesSent, port);
                return static_cast<int>(bytesSent);

            } catch (const std::exception &e) {
                SPDLOG_ERROR("Error sending UDP broadcast: {}", e.what());
                return -1;
            }
        }

        int sendUdp(const std::string &host, uint16_t port, const std::vector<uint8_t> &data) {
            try {
                if (!m_udpSocket || !m_udpSocket->is_open()) {
                    SPDLOG_ERROR("UDP socket not initialized");
                    return -1;
                }

                // Resolve the host
                asio::ip::udp::resolver resolver(m_ioContext);
                auto endpoints = resolver.resolve(host, std::to_string(port));

                // Get the first element
                asio::ip::udp::endpoint endpoint = *endpoints.begin();

                // Send the data
                auto bytesSent = m_udpSocket->send_to(
                        asio::buffer(data.data(), data.size()),
                        endpoint
                );

                SPDLOG_DEBUG("Sent {} bytes to UDP endpoint {}:{}", bytesSent, host, port);
                return static_cast<int>(bytesSent);
            } catch (const std::exception &e) {
                SPDLOG_ERROR("Error sending UDP data: {}", e.what());
                return -1;
            }
        }

        void shutdown() {
            if (!m_running.exchange(false)) {
                return; // Already shut down
            }

            SPDLOG_INFO("Shutting down SocketHandler");

            try {
                // Close the TCP acceptor if it exists
                if (m_tcpAcceptor && m_tcpAcceptor->is_open()) {
                    m_tcpAcceptor->close();
                }

                // Close the UDP socket if it exists
                if (m_udpSocket && m_udpSocket->is_open()) {
                    m_udpSocket->close();
                }

                // Close all the TCP sockets
                {
                    std::lock_guard<std::mutex> lock(m_socketsMutex);
                    for (auto &[endpoint, socket]: m_tcpSockets) {
                        if (socket->is_open()) {
                            asio::error_code ec;
                            socket->close(ec);
                            if (ec) {
                                SPDLOG_WARN("Error closing socket for {}: {}", endpoint, ec.message());
                            }
                        }
                    }
                    m_tcpSockets.clear();
                }

                // Stop the IO context
                m_work.reset();
                m_ioContext.stop();

                // Wait for the IO thread to finish
                if (m_ioThread.joinable()) {
                    m_ioThread.join();
                }

                SPDLOG_DEBUG("SocketHandler shutdown complete");

            } catch (const std::exception &e) {
                SPDLOG_ERROR("Exception during SocketHandler shutdown: {}", e.what());
            }
        }
    };

    SocketHandler::SocketHandler() : m_impl(std::make_unique<Impl>()){}

    SocketHandler::~SocketHandler() { };

    bool SocketHandler::initTcpServer(uint16_t port, network::DataReceivedCallback onDataReceived,
                                      network::ConnectionStatusCallback onConnectionStatus) {
        return m_impl->initTcpServer(port, std::move(onDataReceived), std::move(onConnectionStatus));
    }

    bool SocketHandler::connectTcp(const std::string &host, uint16_t port, network::DataReceivedCallback onDataReceived,
                                   network::ConnectionStatusCallback onConnectionStatus) {
        return m_impl->connectTcp(host, port, std::move(onDataReceived), std::move(onConnectionStatus));
    }

    std::future<int> SocketHandler::sendTcp(const std::string &endpoint, const std::vector<uint8_t> &data) {
        return m_impl->sendTcp(endpoint, data);
    }

    bool SocketHandler::initUdpSocket(uint16_t port, network::DataReceivedCallback onDataReceived) {
        return m_impl->initUdpSocket(port, std::move(onDataReceived));
    }

    int SocketHandler::sendUdpBroadcast(uint16_t port, const std::vector<uint8_t> &data) {
        return m_impl->sendUdpBroadcast(port, data);
    }

    int SocketHandler::sendUdp(const std::string &host, uint16_t port, const std::vector<uint8_t> &data) {
        return m_impl->sendUdp(host, port, data);
    }

    void SocketHandler::shutdown() {
        m_impl->shutdown();
    }
}
