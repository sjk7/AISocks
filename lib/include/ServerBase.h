// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
#ifndef AISOCKS_SERVER_BASE_H
#define AISOCKS_SERVER_BASE_H

#include "Poller.h"
#include "TcpSocket.h"
#include <memory>
#include <unordered_map>

namespace aiSocks {

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
        Poller poller;
        if (!poller.add(*listener_, PollEvent::Readable | PollEvent::Error)) {
            throw SocketException(listener_->getLastError(), "ServerBase::run",
                "Failed to register listening socket with Poller", 0, false);
        }

        bool accepting = true;
        size_t accepted = 0;

        while (accepting || !clients_.empty()) {
            auto ready = poller.wait(timeout);
            for (const auto& event : ready) {
                if (event.socket == listener_.get()) {
                    if (!accepting) continue;
                    drainAccept(poller, accepting, accepted, maxClients);
                    continue;
                }

                auto it = clients_.find(event.socket);
                if (it == clients_.end()) continue;

                bool keep = !hasFlag(event.events, PollEvent::Error);
                if (keep && hasFlag(event.events, PollEvent::Readable)) {
                    keep = onReadable(*it->second.socket, it->second.data);
                }
                if (keep && hasFlag(event.events, PollEvent::Writable)) {
                    keep = onWritable(*it->second.socket, it->second.data);
                }

                if (!keep) {
                    onDisconnect(it->second.data);
                    (void)poller.remove(*it->second.socket);
                    clients_.erase(it);
                }
            }
        }
    }

    // Access the underlying listening socket (e.g. to set socket options).
    TcpSocket& getSocket() { return *listener_; }
    const TcpSocket& getSocket() const { return *listener_; }

    // Current number of connected clients.
    size_t clientCount() const { return clients_.size(); }

    protected:
    // -- Override in derived classes ------------------------------------------

    // Called when the client socket is readable.  Return false to disconnect.
    virtual bool onReadable(TcpSocket& sock, ClientData& data) = 0;

    // Called when the client socket is writable.  Return false to disconnect.
    virtual bool onWritable(TcpSocket& sock, ClientData& data) = 0;

    // Called just before a client is removed.  Default: no-op.
    virtual void onDisconnect(ClientData& /*data*/) {}

    private:
    struct ClientEntry {
        std::unique_ptr<TcpSocket> socket;
        ClientData data{};
    };

    std::unique_ptr<TcpSocket> listener_;
    std::unordered_map<const Socket*, ClientEntry> clients_;

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

            const Socket* key = client.get();
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
