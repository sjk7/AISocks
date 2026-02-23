// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
#ifndef AISOCKS_SIMPLE_SERVER_H
#define AISOCKS_SIMPLE_SERVER_H

#include "Poller.h"
#include "TcpSocket.h"
#include "SocketFactory.h"
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
    // Returns invalid socket if bind or listen fails - check isValid().
    SimpleServer(const ServerBind& args,
        AddressFamily family = AddressFamily::IPv4)
        : socket_(std::make_unique<TcpSocket>(TcpSocket::createRaw(family))) {
        // Use SocketFactory to create server without exceptions
        auto result = SocketFactory::createTcpServer(family, args);
        if (result.isSuccess()) {
            *socket_ = std::move(result.value());
            if (!socket_->setBlocking(false)) {
                // Failed to set non-blocking - invalidate socket
                socket_.reset();
            }
        } else {
            // Server creation failed - socket remains invalid
            socket_.reset();
        }
    }

    // Poll-driven accept loop that invokes callback for each accepted client.
    // Callback signature: void(TcpSocket&)
    //
    // Note: This accepts clients in non-blocking mode using Poller, but the
    // callback itself is responsible for any client I/O strategy.
    template <typename Callback>
    void acceptClients(Callback&& onClient, size_t maxClients = 0) {
        if (!socket_ || !socket_->isValid()) return;
        
        Poller poller;
        if (!poller.add(*socket_, PollEvent::Readable | PollEvent::Error)) {
            return; // Failed to register with poller
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
        if (!socket_ || !socket_->isValid()) return;
        
        Poller poller;
        if (!poller.add(*socket_, PollEvent::Readable | PollEvent::Error)) {
            return; // Failed to register with poller
        }

        std::unordered_map<const Socket*, std::unique_ptr<TcpSocket>> clients;
        // Pre-reserve client map if maxClients is specified to eliminate hash table growth
        if (maxClients > 0) {
            clients.reserve(maxClients);
        }
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

    // Check if the server socket is valid and ready for use
    bool isValid() const {
        return socket_ && socket_->isValid();
    }

    // Get access to the underlying server socket.
    // Useful for configuring socket options or checking socket state.
    TcpSocket& getSocket() {
        if (!socket_) {
            static TcpSocket dummy = TcpSocket::createRaw();
            return dummy;
        }
        return *socket_;
    }

    const TcpSocket& getSocket() const {
        if (!socket_) {
            static TcpSocket dummy = TcpSocket::createRaw();
            return dummy;
        }
        return *socket_;
    }

    private:
    std::unique_ptr<TcpSocket> socket_;
};

} // namespace aiSocks

#endif // AISOCKS_SIMPLE_SERVER_H
