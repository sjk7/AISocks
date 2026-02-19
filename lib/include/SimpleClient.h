// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
#ifndef AISOCKS_SIMPLE_CLIENT_H
#define AISOCKS_SIMPLE_CLIENT_H

#include "TcpSocket.h"
#include <functional>
#include <memory>

namespace aiSocks {

// ---------------------------------------------------------------------------
// SimpleClient  one-liner convenience wrapper for TCP client connections.
//
// Connects to a remote server and invokes a callback with the connected socket.
// Useful for quick prototyping and simple request-response patterns.
//
// Usage:
//   SimpleClient client("example.com", 80, [](TcpSocket& sock) {
//       sock.sendAll("GET / HTTP/1.0\r\n\r\n", ...);
//       char buf[4096];
//       int n = sock.receive(buf, sizeof(buf));
//       std::cout.write(buf, n);
//   });
//
// The callback is invoked synchronously in the constructor. If connect fails,
// the callback is not invoked; check isConnected() or getLastError() instead.
// ---------------------------------------------------------------------------
class SimpleClient {
    public:
    // Connect to address:port and invoke callback with the connected socket.
    // Callback signature: void(TcpSocket&)
    // 
    // If connection fails, callback is not called. Check isConnected() after.
    template <typename Callback>
    SimpleClient(const std::string& address, Port port, Callback&& onConnected,
        Milliseconds timeout = defaultTimeout)
        : socket_(nullptr), error_(SocketError::None) {
        try {
            auto sock = std::make_unique<TcpSocket>(AddressFamily::IPv4,
                ConnectArgs{address, port, timeout});
            socket_ = std::move(sock);
            onConnected(*socket_);
        } catch (const SocketException& e) {
            // Connection failed; store error state
            error_ = e.errorCode();
        }
    }

    // Check if the connection succeeded.
    bool isConnected() const noexcept { return socket_ != nullptr; }

    // Access the underlying socket for manual operations.
    TcpSocket& getSocket() {
        if (!socket_) throw std::runtime_error("SimpleClient: not connected");
        return *socket_;
    }

    const TcpSocket& getSocket() const {
        if (!socket_) throw std::runtime_error("SimpleClient: not connected");
        return *socket_;
    }

    // Query last error if connection failed.
    SocketError getLastError() const noexcept { 
        return error_;
    }

    private:
    std::unique_ptr<TcpSocket> socket_;
    SocketError error_;
};

} // namespace aiSocks

#endif // AISOCKS_SIMPLE_CLIENT_H
