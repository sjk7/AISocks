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
#include <iostream>
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
// four virtual hooks:
//
//   ServerResult onReadable(TcpSocket& sock, ClientData& data)
//     Called when the client socket has incoming data.
//     Return ServerResult::Disconnect to disconnect and destroy the client.
//     Return ServerResult::StopServer to stop the server gracefully.
//     Return ServerResult::KeepConnection to continue.
//
//   ServerResult onWritable(TcpSocket& sock, ClientData& data)
//     Called when the client socket buffer has space for more data.
//     Return ServerResult::Disconnect to disconnect and destroy the client.
//     Return ServerResult::StopServer to stop the server gracefully.
//     Return ServerResult::KeepConnection to continue.
//
//   ServerResult onDisconnect(ClientData& data)   [optional]
//     Called just before a client entry is removed (error, or Disconnect
//     return from onReadable/onWritable).  Default returns Disconnect.
//
//   ServerResult onIdle()   [optional]
//     Called on every loop iteration after poller.wait() returns, before
//     processing events. Default returns KeepConnection.
//
// Usage:
//   struct MyState { std::string inbuf; std::string outbuf; size_t sent{}; };
//
//   class MyServer : public ServerBase<MyState> {
//   public:
//       explicit MyServer(const ServerBind& b) : ServerBase(b) {}
//   protected:
//       ServerResult onReadable(TcpSocket& sock, MyState& s) override {
//           return ServerResult::KeepConnection;
//       }
//       ServerResult onWritable(TcpSocket& sock, MyState& s) override {
//           return ServerResult::KeepConnection;
//       }
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
        current_poller_ = &poller;
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
            for (const auto& event : ready) {
                if (event.socket == listener_.get()) {
                    if (!accepting) continue;
                    drainAccept(poller, accepting, accepted, maxClients);
                    continue;
                }

                auto it = clients_.find(event.socket->getNativeHandle());
                if (it == clients_.end()) continue;

                bool keep = !hasFlag(event.events, PollEvent::Error);
                if (!keep) {
                    onError(*it->second.socket, it->second.data);
                }
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
                    it->second.socket->shutdown(ShutdownHow::Both);
                    (void)poller.remove(*it->second.socket);
                    clients_.erase(it);
#ifdef SERVER_STATS
                    std::cout << "[stats] clients: " << clients_.size()
                              << "  max: " << max_clients_ << "\n";
#endif
                }
            }
            if (onIdle() == ServerResult::StopServer) {
                s_stop_.store(true);
                break;
            }
        }

        // Clean up any remaining clients when stopping
        for (auto& [fd, entry] : clients_) {
            onDisconnect(entry.data);
        }
        clients_.clear();
        current_poller_ = nullptr;
    }

    // Access the underlying listening socket (e.g. to set socket options).
    TcpSocket& getSocket() { return *listener_; }
    const TcpSocket& getSocket() const { return *listener_; }

    // Current number of connected clients.
    size_t clientCount() const { return clients_.size(); }

    // Peak concurrent client count since server started.
    size_t peakClientCount() const { return peak_clients_; }

    // Mark a client socket as active (resets the keep-alive idle timer).
    // Call this after a successful read or write.
    void touchClient(const TcpSocket& sock) {
        auto it = clients_.find(sock.getNativeHandle());
        if (it != clients_.end()) it->second.lastActivity = SteadyClock::now();
    }

    // Enable or disable Writable interest for a client socket.
    // Call with true when you have data to send, false when done sending.
    void setClientWritable(const TcpSocket& sock, bool writable) {
        if (!current_poller_) return;
        PollEvent interest = PollEvent::Readable | PollEvent::Error;
        if (writable) interest = interest | PollEvent::Writable;
        current_poller_->modify(sock, interest);
    }

    // Keep-alive idle timeout. Connections that have been idle longer than
    // this will be closed gracefully. Set to 0 to disable. Default: 65s.
    void setKeepAliveTimeout(std::chrono::seconds timeout) {
        keepAliveTimeout_ = timeout;
    }
    std::chrono::seconds getKeepAliveTimeout() const {
        return keepAliveTimeout_;
    }

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

    // Called when the client socket is readable.
    virtual ServerResult onReadable(TcpSocket& sock, ClientData& data) = 0;

    // Called when the client socket is writable.
    virtual ServerResult onWritable(TcpSocket& sock, ClientData& data) = 0;

    // Called just before a client is removed.  Default: no-op.
    virtual ServerResult onDisconnect(ClientData& /*data*/) {
        return ServerResult::Disconnect;
    }

    // Called when a poll error event fires on a client socket.
    // sock is still valid at this point. Default: no-op.
    virtual void onError(TcpSocket& /*sock*/, ClientData& /*data*/) {}

    // Called after the keep-alive sweep closes one or more idle connections.
    // Default: prints the count to stdout.
    virtual void onClientsTimedOut(size_t count) {
        std::cout << "[keepalive] closed " << count << " idle connection"
                  << (count == 1 ? "" : "s") << "\n";
    }

    // Called on every loop iteration after poller.wait() returns, before
    // processing events. Use this to do periodic bookkeeping on the server
    // thread without spawning extra threads. For reliable periodic calls,
    // pass a bounded timeout to run() (e.g. Milliseconds{100}).
    // Derived classes should call ServerBase::onIdle() to retain keep-alive
    // timeout behaviour.
    virtual ServerResult onIdle() {
        if (!current_poller_ || keepAliveTimeout_.count() == 0)
            return ServerResult::KeepConnection;
        auto now = std::chrono::steady_clock::now();
        if (now - last_idle_check_ < std::chrono::seconds{1})
            return ServerResult::KeepConnection;
        last_idle_check_ = now;
        size_t timedOut = 0;
        for (auto it = clients_.begin(); it != clients_.end();) {
            auto idle = std::chrono::duration_cast<std::chrono::seconds>(
                now - it->second.lastActivity);
            if (idle >= keepAliveTimeout_) {
                onDisconnect(it->second.data);
                it->second.socket->shutdown(ShutdownHow::Write);
                (void)current_poller_->remove(*it->second.socket);
                it = clients_.erase(it);
                ++timedOut;
            } else {
                ++it;
            }
        }
        if (timedOut > 0) onClientsTimedOut(timedOut);
        return ServerResult::KeepConnection;
    }

    private:
    using SteadyClock = std::chrono::steady_clock;
    struct ClientEntry {
        std::unique_ptr<TcpSocket> socket;
        ClientData data{};
        SteadyClock::time_point lastActivity{SteadyClock::now()};
    };

    inline static std::atomic<bool> s_stop_{false};
#ifdef SERVER_STATS
    size_t max_clients_{0};
#endif

    static void handleSignal(int) {
        s_stop_.store(true, std::memory_order_relaxed);
    }

    Poller* current_poller_{nullptr};
    std::unique_ptr<TcpSocket> listener_;
    std::unordered_map<uintptr_t, ClientEntry> clients_;
    std::chrono::seconds keepAliveTimeout_{65};
    size_t peak_clients_{0};
    SteadyClock::time_point last_idle_check_{SteadyClock::now()};

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
            client->setNoDelay(true);

            uintptr_t key = client->getNativeHandle();
            if (!poller.add(*client, PollEvent::Readable | PollEvent::Error)) {
                continue;
            }

            clients_.emplace(key, ClientEntry{std::move(client), ClientData{}});
            ++accepted;
            if (clients_.size() > peak_clients_)
                peak_clients_ = clients_.size();
#ifdef SERVER_STATS
            std::cout << "[stats] clients: " << clients_.size()
                      << "  peak: " << peak_clients_ << "\n";
#endif

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
