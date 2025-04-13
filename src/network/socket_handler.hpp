#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <future>
#include <asio.hpp>

namespace network {

    /**
     * Connection status enum
     */
    enum class ConnectionStatus {
        Connected,
        Disconnected,
        Error
    };

    /**
     * Callback for data reception
     * @param data Data received
     * @param endpoint Source endpoint
     */
    using DataReceivedCallback = std::function<void(const std::vector<uint8_t> &data,
                                                    const std::string &endpoint)>;

    /**
     * Callback for connection status changes
     * @param status New connection status
     * @param endpoint Remote endpoint
     * @param errorMessage Error message (if status is Error)
     */
    using ConnectionStatusCallback = std::function<void(ConnectionStatus status,
                                                        const std::string &endpoint,
                                                        const std::string &errorMessage)>;

    /**
     * Handles low-level socket operations for TCP and UDP communication
     */
    class SocketHandler{
    public:
        /**
         * Constructor
         */
        SocketHandler();

        /**
         * Destructor
         */
        ~SocketHandler();

        /**
         * Initialize TCP server on a specific port
         * @param port Port to listen on
         * @param onDataReceived Callback for data reception
         * @param onConnectionStatus Callback for connection status changes
         * @return True if initialization was successful, false otherwise
         */
        bool initTcpServer(uint16_t port, DataReceivedCallback onDataReceived,
                           ConnectionStatusCallback onConnectionStatus);

        /**
         * Connect to a TCP server
         * @param host Host to connect to (IP or hostname)
         * @param port Port to connect to
         * @param onDataReceived Callback for data reception
         * @param onConnectionStatus Callback for connection status changes
         * @return True if connection was initiated, false otherwise
         */
        bool connectTcp(const std::string& host, uint16_t port,
                        DataReceivedCallback onDataReceived,
                        ConnectionStatusCallback onConnectionStatus);

        /**
         * Send data to a TCP connection
         * @param endpoint Endpoint to send to (in format "host:port")
         * @param data Data to send
         * @return Future that resolves to number of bytes sent or -1 on error
         */
        std::future<int> sendTcp(const std::string& endpoint,
                                 const std::vector<uint8_t>& data);

        /**
         * Initialize UDP socket for broadcasting/discovery
         * @param port Port to listen on
         * @param onDataReceived Callback for data reception
         * @return True if initialization was successful, false otherwise
         */
        bool initUdpSocket(uint16_t port, DataReceivedCallback onDataReceived);

        /**
         * Send UDP broadcast message
         * @param port Port to broadcast to
         * @param data Data to send
         * @return Number of bytes sent or -1 on error
         */
        int sendUdpBroadcast(uint16_t port, const std::vector<uint8_t>& data);

        /**
         * Send UDP datagram to a specific host
         * @param host Host to send to
         * @param port Port to send to
         * @param data Data to send
         * @return Number of bytes sent or -1 on error
         */
        int sendUdp(const std::string& host, uint16_t port,
                    const std::vector<uint8_t>& data);

        /**
         * Close all connections and stop all operations
         */
        void shutdown();
    private:
        class Impl;
        std::unique_ptr<Impl> m_impl;
    };


}
