// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
#ifndef AISOCKS_SERVER_BASE_H
#define AISOCKS_SERVER_BASE_H

#include "Poller.h"
#include "TcpSocket.h"
#include <atomic>
#include <csignal>
#include <functional>
#include <memory>
#include <unordered_map>

namespace aiSocks {

// Return values for ServerBase virtual functions
enum class ServerResult {
    KeepConnection = 1, // Keep the connection alive
    Disconnect = 0, // Disconnect this client
    StopServer = -1 // Stop the server gracefully
};

// ---------------------------------------------------------------------------
// ServerBase<ClientData>  CRTP-free base for poll-driven TCP servers.
//
// Manages the listening socket, Poller registration, client socket lifetime,
// and per-client state storage.  Derived classes only need to implement the
// three virtual hooks:
//
//   bool onReadable(TcpSocket& sock, ClientData& data)
//     Called when the client socket has incoming data.
//     Return false to disconnect and destroy the client.
//
//   bool onWritable(TcpSocket& sock, ClientData& data)
//     Called when the client socket buffer has space for more data.
//     Return false to disconnect and destroy the client.
//
//   void onDisconnect(ClientData& data)   [optional]
//     Called just before a client entry is removed (error, or false return
//     from onReadable/onWritable).  Default is a no-op.
//
// Usage:
//   struct MyState { std::string inbuf; std::string outbuf; size_t sent{}; };
//
//   class MyServer : public ServerBase<MyState> {
//   public:
//       explicit MyServer(const ServerBind& b) : ServerBase(b) {}
//   protected:
//       bool onReadable(TcpSocket& sock, MyState& s) override { ... }
//       bool onWritable(TcpSocket& sock, MyState& s) override { ... }
//   };
//
//   MyServer srv(ServerBind{"0.0.0.0", Port{9000}});
//   srv.run();
//
// Throws SocketException if bind, listen, or Poller registration fails.
// run() blocks until there are no more clients and accepting has stopped.
// ---------------------------------------------------------------------------

template <typename ClientData> class ServerBase {
    public:
    // Construct and start listening.  Does not accept until run() is called.
    // Throws SocketException on failure.
    explicit ServerBase(
        const ServerBind& args, AddressFamily family = AddressFamily::IPv4)
        : listener_(std::make_unique<TcpSocket>(family, args)) {
        if (!listener_->setBlocking(false)) {
            throw SocketException(listener_->getLastError(),
                "ServerBase::ServerBase",
                "Failed to set listening socket to non-blocking", 0, false);
        }
    }

    virtual ~ServerBase() = default;

    // Non-copyable, movable.
    ServerBase(const ServerBase&) = delete;
    ServerBase& operator=(const ServerBase&) = delete;
    ServerBase(ServerBase&&) = default;
    ServerBase& operator=(ServerBase&&) = default;

    // Enter the poll loop.
    //
    // maxClients: 0 = unlimited; N > 0 = stop accepting after N connections,
    //             but continue serving existing clients until all disconnect.
    // timeout:    passed to Poller::wait(); -1 = block until an event.
    //
    // Returns when there are no remaining connected clients (and accepting is
    // stopped, either because maxClients was reached or you stopped
    // externally).
    void run(size_t maxClients = 0, Milliseconds timeout = Milliseconds{-1}) {
        s_stop_.store(false, std::memory_order_relaxed);

        // Install SIGINT/SIGTERM for Ctrl+C shutdown; restore on exit.
        auto prevInt = std::signal(SIGINT, handleSignal);
        auto prevTerm = std::signal(SIGTERM, handleSignal);
        struct SigGuard {
            void (*pi)(int);
            void (*pt)(int);
            ~SigGuard() {
                std::signal(SIGINT, pi);
                std::signal(SIGTERM, pt);
            }
        } guard{prevInt, prevTerm};

        Poller poller;
        if (!poller.add(*listener_, PollEvent::Readable | PollEvent::Error)) {
            throw SocketException(listener_->getLastError(), "ServerBase::run",
                "Failed to register listening socket with Poller", 0, false);
        }

        bool accepting = true;
        size_t accepted = 0;

        while (!s_stop_.load(std::memory_order_relaxed)
            && (accepting || !clients_.empty())) {
            auto ready = poller.wait(timeout);
            if (s_stop_.load(std::memory_order_relaxed)) break;
            if (onIdle() == ServerResult::StopServer) {
                s_stop_.store(true);
                break;
            }
            for (const auto& event : ready) {
                if (event.socket == listener_.get()) {
                    if (!accepting) continue;
                    drainAccept(poller, accepting, accepted, maxClients);
                    continue;
                }

                auto it = clients_.find(event.socket->getNativeHandle());
                if (it == clients_.end()) continue;

                bool keep = !hasFlag(event.events, PollEvent::Error);
                if (keep && hasFlag(event.events, PollEvent::Readable)) {
                    ServerResult result
                        = onReadable(*it->second.socket, it->second.data);
                    if (result == ServerResult::StopServer) {
                        s_stop_.store(true);
                        break;
                    }
                    keep = (result == ServerResult::KeepConnection);
                }
                if (keep && hasFlag(event.events, PollEvent::Writable)) {
                    ServerResult result
                        = onWritable(*it->second.socket, it->second.data);
                    if (result == ServerResult::StopServer) {
                        s_stop_.store(true);
                        break;
                    }
                    keep = (result == ServerResult::KeepConnection);
                }

                if (!keep) {
                    onDisconnect(it->second.data);
                    (void)poller.remove(*it->second.socket);
                    clients_.erase(it);
                }
            }
        }

        // Clean up any remaining clients when stopping
        for (auto& [fd, entry] : clients_) {
            onDisconnect(entry.data);
        }
        clients_.clear();
    }

    // Access the underlying listening socket (e.g. to set socket options).
    TcpSocket& getSocket() { return *listener_; }
    const TcpSocket& getSocket() const { return *listener_; }

    // Current number of connected clients.
    size_t clientCount() const { return clients_.size(); }

    // Optimized send with large chunks for better throughput
    int sendOptimized(TcpSocket& sock, const char* data, size_t size) {
        const size_t CHUNK_SIZE = 64 * 1024; // 64KB chunks
        size_t sent = 0;

        while (sent < size) {
            size_t to_send = std::min(CHUNK_SIZE, size - sent);
            int n = sock.send(data + sent, to_send);
            if (n <= 0) {
                return sent > 0 ? static_cast<int>(sent) : n;
            }
            sent += n;

            // If we couldn't send the full chunk, socket buffer is full
            if (n < static_cast<int>(to_send)) {
                break;
            }
        }

        return static_cast<int>(sent);
    }

    // Request a graceful shutdown. Safe to call from a signal handler or any
    // thread. run() will exit after the current wait() returns.
    static void requestStop() noexcept {
        s_stop_.store(true, std::memory_order_relaxed);
    }
    static bool stopRequested() noexcept {
        return s_stop_.load(std::memory_order_relaxed);
    }

    protected:
    // -- Override in derived classes ------------------------------------------

    // Called when the client socket is readable.  Return 0 to disconnect.
    virtual ServerResult onReadable(TcpSocket& sock, ClientData& data) = 0;

    // Called when the client socket is writable.  Return 0 to disconnect.
    virtual ServerResult onWritable(TcpSocket& sock, ClientData& data) = 0;

    // Called just before a client is removed.  Default: no-op.
    virtual ServerResult onDisconnect(ClientData& /*data*/) {
        return ServerResult::Disconnect;
    }

    // Called on every loop iteration after poller.wait() returns, before
    // processing events. Use this to do periodic bookkeeping on the server
    // thread without spawning extra threads. For reliable periodic calls,
    // pass a bounded timeout to run() (e.g. Milliseconds{100}).
    virtual ServerResult onIdle() { return ServerResult::KeepConnection; }

    private:
    struct ClientEntry {
        std::unique_ptr<TcpSocket> socket;
        ClientData data{};
    };

    inline static std::atomic<bool> s_stop_{false};

    static void handleSignal(int) {
        s_stop_.store(true, std::memory_order_relaxed);
    }

    std::unique_ptr<TcpSocket> listener_;
    std::unordered_map<uintptr_t, ClientEntry> clients_;

    void drainAccept(
        Poller& poller, bool& accepting, size_t& accepted, size_t maxClients) {
        for (;;) {
            auto client = listener_->accept();
            if (!client) {
                // WouldBlock or hard error -- either way stop draining.
                break;
            }
            if (!client->setBlocking(false)) {
                continue; // couldn't set non-blocking; drop this client
            }

            uintptr_t key = client->getNativeHandle();
            if (!poller.add(*client,
                    PollEvent::Readable | PollEvent::Writable
                        | PollEvent::Error)) {
                continue;
            }

            clients_.emplace(key, ClientEntry{std::move(client), ClientData{}});
            ++accepted;

            if (maxClients != 0 && accepted >= maxClients) {
                (void)poller.remove(*listener_);
                accepting = false;
                break;
            }
        }
    }
};

} // namespace aiSocks

#endif // AISOCKS_SERVER_BASE_H
