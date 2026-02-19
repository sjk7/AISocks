// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
#ifndef AISOCKS_SIMPLE_SERVER_H
#define AISOCKS_SIMPLE_SERVER_H

#include "Poller.h"
#include "TcpSocket.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace aiSocks {

// ---------------------------------------------------------------------------
// SimpleServer  convenience wrapper for TCP server polling loops.
//
// Creates a non-blocking listening socket and drives a Poller-based event
// loop for accept/read/write readiness. Useful for quick prototyping and
// simple single-threaded server patterns.
//
// Usage:
//   try {
//       SimpleServer server(ServerBind{"0.0.0.0", Port{8080}});
//       server.pollClients([](TcpSocket& client, PollEvent events) {
//           if (hasFlag(events, PollEvent::Readable)) {
//               char buf[1024];
//               int n = client.receive(buf, sizeof(buf));
//               if (n > 0) {
//                   (void)client.send(buf, static_cast<size_t>(n));
//               }
//           }
//           return true; // keep client connected
//       });
//   } catch (const SocketException& e) {
//       std::cerr << "Server failed: " << e.what() << "\n";
//   }
//
// Throws SocketException if socket creation, bind, or listen fails.
// pollClients() uses non-blocking sockets throughout and Poller readiness for:
//   - accepting clients
//   - readable client data
//   - writable client buffer space
// ---------------------------------------------------------------------------
class SimpleServer {
    public:
    // Create a listening server socket.
    // Does NOT start accepting connections yet - call acceptClients() to start.
    // 
    // Throws SocketException if bind or listen fails.
    SimpleServer(const ServerBind& args,
        AddressFamily family = AddressFamily::IPv4)
        : socket_(std::make_unique<TcpSocket>(family, args)) {
        if (!socket_->setBlocking(false)) {
            throw SocketException(socket_->getLastError(),
                "SimpleServer::SimpleServer",
                "Failed to set server socket to non-blocking mode", 0, false);
        }
    }

    // Poll-driven accept loop that invokes callback for each accepted client.
    // Callback signature: void(TcpSocket&)
    //
    // Note: This accepts clients in non-blocking mode using Poller, but the
    // callback itself is responsible for any client I/O strategy.
    template <typename Callback>
    void acceptClients(Callback&& onClient, size_t maxClients = 0) {
        Poller poller;
        if (!poller.add(*socket_, PollEvent::Readable | PollEvent::Error)) {
            throw SocketException(socket_->getLastError(),
                "SimpleServer::acceptClients",
                "Failed to register listening socket with Poller", 0, false);
        }

        size_t count = 0;
        while (maxClients == 0 || count < maxClients) {
            auto ready = poller.wait(Milliseconds{-1});
            for (const auto& event : ready) {
                if (event.socket != socket_.get()) continue;
                if (!hasFlag(event.events, PollEvent::Readable)
                    && !hasFlag(event.events, PollEvent::Error)) {
                    continue;
                }

                for (;;) {
                    auto client = socket_->accept();
                    if (!client) {
                        auto err = socket_->getLastError();
                        if (err == SocketError::WouldBlock) {
                            break; // drained all pending accepts
                        }
                        break; // hard accept error; continue event loop
                    }

                    if (!client->setBlocking(false)) {
                        continue;
                    }

                    onClient(*client);
                    ++count;
                    if (maxClients != 0 && count >= maxClients) {
                        return;
                    }
                }
            }
        }
    }

    // Full Poller-driven server loop for accept + readable + writable events.
    // Callback signature: bool(TcpSocket&, PollEvent)
    //
    // Return value:
    //   true  keep the client registered.
    //   false remove and close the client.
    //
    // maxClients semantics:
    //   0              accept forever.
    //   N > 0          accept up to N clients, then stop accepting and keep
    //                  polling existing clients until all disconnect.
    template <typename Callback>
    void pollClients(Callback&& onClientEvent, size_t maxClients = 0,
        Milliseconds timeout = Milliseconds{-1}) {
        Poller poller;
        if (!poller.add(*socket_, PollEvent::Readable | PollEvent::Error)) {
            throw SocketException(socket_->getLastError(),
                "SimpleServer::pollClients",
                "Failed to register listening socket with Poller", 0, false);
        }

        std::unordered_map<const Socket*, std::unique_ptr<TcpSocket>> clients;
        size_t accepted = 0;
        bool accepting = true;

        while (accepting || !clients.empty()) {
            auto ready = poller.wait(timeout);
            for (const auto& event : ready) {
                if (event.socket == socket_.get()) {
                    if (!accepting) continue;
                    if (!hasFlag(event.events, PollEvent::Readable)
                        && !hasFlag(event.events, PollEvent::Error)) {
                        continue;
                    }

                    for (;;) {
                        auto client = socket_->accept();
                        if (!client) {
                            auto err = socket_->getLastError();
                            if (err == SocketError::WouldBlock) break;
                            break;
                        }

                        if (!client->setBlocking(false)) {
                            continue;
                        }

                        const Socket* key = client.get();
                        if (!poller.add(*client,
                                PollEvent::Readable | PollEvent::Writable
                                    | PollEvent::Error)) {
                            continue;
                        }

                        clients.emplace(key, std::move(client));
                        ++accepted;

                        if (maxClients != 0 && accepted >= maxClients) {
                            (void)poller.remove(*socket_);
                            accepting = false;
                            break;
                        }
                    }
                    continue;
                }

                auto it = clients.find(event.socket);
                if (it == clients.end()) continue;

                bool keepClient = !hasFlag(event.events, PollEvent::Error);
                if (keepClient) {
                    keepClient = onClientEvent(*it->second, event.events);
                }

                if (!keepClient) {
                    (void)poller.remove(*it->second);
                    clients.erase(it);
                }
            }
        }
    }

    // Get access to the underlying server socket.
    // Useful for configuring socket options or checking socket state.
    TcpSocket& getSocket() {
        return *socket_;
    }

    const TcpSocket& getSocket() const {
        return *socket_;
    }

    private:
    std::unique_ptr<TcpSocket> socket_;
};

} // namespace aiSocks

#endif // AISOCKS_SIMPLE_SERVER_H
