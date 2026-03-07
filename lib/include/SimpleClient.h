// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
#ifndef AISOCKS_SIMPLE_CLIENT_H
#define AISOCKS_SIMPLE_CLIENT_H

#include "TcpSocket.h"
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>

namespace aiSocks {

// ---------------------------------------------------------------------------
// SimpleClient  one-liner convenience wrapper for TCP client connections.
//
// Connects to a remote server and invokes a callback with the connected socket.
// Useful for quick prototyping and simple request-response patterns.
//
// Usage:
//   try {
//       SimpleClient client(ConnectArgs{"example.com", 80}, [](TcpSocket& sock) {
//           sock.sendAll("GET / HTTP/1.0\r\n\r\n", ...);
//           char buf[4096];
//           int n = sock.receive(buf, sizeof(buf));
//           std::cout.write(buf, n);
//       });
//   } catch (const SocketException& e) {
//       std::cerr << "Connection failed: " << e.what() << "\n";
//   }
//
// Throws SocketException if connection fails (consistent with TcpSocket).
// The callback is invoked synchronously in the constructor immediately after
// a successful connection.
// ---------------------------------------------------------------------------
class SimpleClient {
    public:
    // Connect using ConnectArgs and invoke callback with the connected socket.
    // Callback signature: void(TcpSocket&)
    // 
    // Throws SocketException if connection fails (lets it propagate from TcpSocket).
    template <typename Callback>
    SimpleClient(const ConnectArgs& args, Callback&& onConnected,
        AddressFamily family = AddressFamily::IPv4) {
        auto sock = std::make_unique<TcpSocket>(family, args);
        // Set receive timeout to prevent indefinite blocking
        // Use the connection timeout as the receive timeout
        sock->setReceiveTimeout(args.connectTimeout);
        socket_ = std::move(sock);
        onConnected(*socket_);
    }

    // Check if the connection was established (always true if constructor succeeded).
    bool isConnected() const noexcept { return socket_ != nullptr; }

    // Access the underlying socket for manual operations.
    TcpSocket& getSocket() {
        if (!socket_) throw std::runtime_error("SimpleClient: socket not initialized");
        return *socket_;
    }

    const TcpSocket& getSocket() const {
        if (!socket_) throw std::runtime_error("SimpleClient: socket not initialized");
        return *socket_;
    }

    private:
    std::unique_ptr<TcpSocket> socket_;
};

} // namespace aiSocks

#endif // AISOCKS_SIMPLE_CLIENT_H
