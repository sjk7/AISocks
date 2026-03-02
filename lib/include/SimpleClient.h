// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
#ifndef AISOCKS_SIMPLE_CLIENT_H
#define AISOCKS_SIMPLE_CLIENT_H

#include "TcpSocket.h"
#include "SocketFactory.h"
#include <memory>
#include <string>

namespace aiSocks {

// ---------------------------------------------------------------------------
// SimpleClient  convenience wrapper for TCP client connections.
//
// Two usage patterns, in order of preference:
//
//  1. Static factory (most testable — no SimpleClient object needed):
//
//       auto r = SimpleClient::connect(ConnectArgs{"example.com", Port{80}});
//       if (r.isSuccess()) {
//           TcpSocket sock = std::move(r.value());
//           sock.sendAll(...);
//       }
//
//  2. Two-step (construct then run — clean separation, async-friendly):
//
//       SimpleClient client(ConnectArgs{"example.com", Port{80}});
//       if (client.isConnected())
//           client.run([](TcpSocket& sock) { sock.sendAll(...); });
//
// On connection failure the object is left in a disconnected state;
// check isConnected() before calling run() or getSocket() (which returns
// nullptr).
// ---------------------------------------------------------------------------
class SimpleClient {
    public:
    // ── Primary constructor: connect only ────────────────────────────────────
    //
    // Establishes the TCP connection and sets the receive timeout to
    // connectTimeout.  No callback fires; the object is fully constructed
    // before any user code runs.
    explicit SimpleClient(
        const ConnectArgs& args, AddressFamily family = AddressFamily::IPv4) {
        auto result = SocketFactory::createTcpClient(family, args);
        if (result.isSuccess()) {
            socket_ = std::make_unique<TcpSocket>(std::move(result.value()));
            (void)socket_->setReceiveTimeout(args.connectTimeout);
        }
    }

    // ── Named run step ───────────────────────────────────────────────────────
    //
    // Invokes callback(socket) if the connection is live.  Returns true if
    // the callback was called, false if not connected.  Any exception thrown
    // by the callback propagates from a fully-constructed object — the socket
    // is always cleaned up correctly by the destructor.
    //
    // Callback signature: void(TcpSocket&)
    template <typename Callback> bool run(Callback&& callback) {
        if (!socket_) return false;
        std::forward<Callback>(callback)(*socket_);
        return true;
    }

    // ── Static factory ───────────────────────────────────────────────────────
    //
    // Returns the socket directly.  No SimpleClient object is created, making
    // this the easiest form to unit-test in isolation.
    //
    //   auto r = SimpleClient::connect(args);
    //   if (r.isSuccess()) { TcpSocket s = std::move(r.value()); ... }
    static Result<TcpSocket> connect(
        const ConnectArgs& args, AddressFamily family = AddressFamily::IPv4) {
        return SocketFactory::createTcpClient(family, args);
    }

    // ── Observers ────────────────────────────────────────────────────────────
    bool isConnected() const noexcept { return socket_ != nullptr; }

    // Returns a human-readable description of the last socket error, or an
    // empty string if not connected / no error recorded.
    std::string getLastError() const {
        if (!socket_) return {};
        return socket_->getErrorMessage();
    }

    // Direct socket access for callers that want to perform I/O without run().
    // Returns nullptr if not connected — always check before dereferencing.
    [[nodiscard]] TcpSocket* getSocket() noexcept { return socket_.get(); }
    [[nodiscard]] const TcpSocket* getSocket() const noexcept {
        return socket_.get();
    }

    private:
    std::unique_ptr<TcpSocket> socket_;
};

} // namespace aiSocks

#endif // AISOCKS_SIMPLE_CLIENT_H
