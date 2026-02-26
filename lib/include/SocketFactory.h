// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
#ifndef AISOCKS_SOCKET_FACTORY_H
#define AISOCKS_SOCKET_FACTORY_H

#include "Result.h"
#include "Socket.h"
#include "TcpSocket.h"
#include "UdpSocket.h"
#include <memory>

namespace aiSocks {

// ---------------------------------------------------------------------------
// SocketFactory - Exception-free socket creation using Result<T>
// ---------------------------------------------------------------------------
class SocketFactory {
    public:
    // -----------------------------------------------------------------------
    // Basic socket creation
    // -----------------------------------------------------------------------

    // Create a basic TCP socket (not bound or connected)
    static Result<TcpSocket> createTcpSocket(
        AddressFamily family = AddressFamily::IPv4);

    // Create a basic UDP socket (not bound)
    static Result<UdpSocket> createUdpSocket(
        AddressFamily family = AddressFamily::IPv4);

    // -----------------------------------------------------------------------
    // Server socket creation
    // -----------------------------------------------------------------------

    // Create a TCP server socket (bound and listening)
    static Result<TcpSocket> createTcpServer(
        AddressFamily family, const ServerBind& config);

    // Convenience overload for IPv4
    static Result<TcpSocket> createTcpServer(const ServerBind& config) {
        return createTcpServer(AddressFamily::IPv4, config);
    }

    // -----------------------------------------------------------------------
    // Client socket creation
    // -----------------------------------------------------------------------

    // Create a TCP client socket (connected)
    static Result<TcpSocket> createTcpClient(
        AddressFamily family, const ConnectArgs& config);

    // Convenience overload for IPv4
    static Result<TcpSocket> createTcpClient(const ConnectArgs& config) {
        return createTcpClient(AddressFamily::IPv4, config);
    }

    // -----------------------------------------------------------------------
    // UDP socket creation with bind
    // -----------------------------------------------------------------------

    // Create a UDP socket bound to specific address
    static Result<UdpSocket> createUdpServer(
        AddressFamily family, const ServerBind& config);

    // Convenience overload for IPv4
    static Result<UdpSocket> createUdpServer(const ServerBind& config) {
        return createUdpServer(AddressFamily::IPv4, config);
    }

    // -----------------------------------------------------------------------
    // Advanced creation options
    // -----------------------------------------------------------------------

    // Create socket with custom options (for advanced use cases)
    static Result<TcpSocket> createTcpSocketRaw(
        AddressFamily family = AddressFamily::IPv4);

    static Result<UdpSocket> createUdpSocketRaw(
        AddressFamily family = AddressFamily::IPv4);

    // -----------------------------------------------------------------------
    // Utility methods
    // -----------------------------------------------------------------------

    // Check if a port is available for binding
    static Result<bool> isPortAvailable(
        AddressFamily family, const std::string& address, Port port);

    // Find an available port in a range
    static Result<Port> findAvailablePort(AddressFamily family,
        const std::string& address,
        Port startPort = Port{49152}, // Start of ephemeral range
        Port endPort = Port{65535});

    private:
    // Helper methods for error handling
    template <typename SocketType>
    static Result<SocketType> createSocketFromImpl(
        std::unique_ptr<SocketImpl> impl, const char* operation,
        AddressFamily family = AddressFamily::IPv4);

    // Helper to extract error context from SocketImpl
    static Result<void> checkSocketError(
        const Socket& socket, const char* operation);

    // Platform-specific error code extraction
    static int captureLastError();

    // Helper for binding operations
    template <typename SocketType>
    static Result<SocketType> bindSocket(
        SocketType&& socket, const ServerBind& config);

    // Helper for listen operations
    static Result<TcpSocket> listenSocket(TcpSocket&& socket, int backlog);

    // Helper for connect operations
    static Result<TcpSocket> connectSocket(
        TcpSocket&& socket, const ConnectArgs& config);
};

// ---------------------------------------------------------------------------
// Inline implementations for performance-critical methods
// ---------------------------------------------------------------------------

inline Result<TcpSocket> SocketFactory::createTcpSocket(AddressFamily family) {
    return createTcpSocketRaw(family);
}

inline Result<UdpSocket> SocketFactory::createUdpSocket(AddressFamily family) {
    return createUdpSocketRaw(family);
}

} // namespace aiSocks

#endif // AISOCKS_SOCKET_FACTORY_H
