// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
#ifndef AISOCKS_SIMPLE_CLIENT_H
#define AISOCKS_SIMPLE_CLIENT_H

#include "Result.h"
#include "SocketFactory.h"
#include "TcpSocket.h"
#include <memory>

namespace aiSocks {

// ---------------------------------------------------------------------------
// SimpleClient  one-liner convenience wrapper for TCP client connections.
//
// Connects to a remote server and invokes a callback with the connected socket.
// Useful for quick prototyping and simple request-response patterns.
//
// Two usage patterns:
//
//   // 1. Two-step: construct then run
//   SimpleClient client(ConnectArgs{"example.com", Port{80}});
//   if (!client.isConnected()) { /* handle error */ }
//   client.execute([](TcpSocket& sock) { ... });
//
//   // 2. Static factory returning Result<TcpSocket>
//   auto r = SimpleClient::connect(ConnectArgs{"example.com", Port{80}});
//   if (!r.isSuccess()) { /* handle r.errorMessage() */ }
//   TcpSocket sock = std::move(r.value());
//
// Never throws.
// ---------------------------------------------------------------------------
class SimpleClient {
    public:
    // Connect to the server described by args.  Never throws.
    // On failure isConnected() returns false; inspect getLastError().
    explicit SimpleClient(
        const ConnectArgs& args, AddressFamily family = AddressFamily::IPv4)
        : socket_(std::make_unique<TcpSocket>(family, args)) {
        if (socket_->isValid()) socket_->setReceiveTimeout(args.connectTimeout);
    }

    // Invokes cb and returns true if connected; returns false and skips cb
    // otherwise.  Callback signature: void(TcpSocket&)
    template <typename Callback> bool execute(Callback&& cb) {
        if (!isConnected()) return false;
        cb(*socket_);
        return true;
    }

    // Returns true if the connection was established.
    bool isConnected() const noexcept { return socket_ && socket_->isValid(); }

    // Returns nullptr if not connected.
    TcpSocket* getSocket() noexcept {
        return isConnected() ? socket_.get() : nullptr;
    }
    const TcpSocket* getSocket() const noexcept {
        return isConnected() ? socket_.get() : nullptr;
    }

    // Returns the last socket error (SocketError::None on success).
    SocketError getLastError() const noexcept {
        return socket_ ? socket_->getLastError() : SocketError::InvalidSocket;
    }

    // Static factory: returns Result<TcpSocket>.
    static Result<TcpSocket> connect(
        const ConnectArgs& args, AddressFamily family = AddressFamily::IPv4) {
        return SocketFactory::createTcpClient(family, args);
    }

    private:
    std::unique_ptr<TcpSocket> socket_;
};

} // namespace aiSocks

#endif // AISOCKS_SIMPLE_CLIENT_H
