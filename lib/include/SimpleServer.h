// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#ifndef AISOCKS_SIMPLE_SERVER_H
#define AISOCKS_SIMPLE_SERVER_H

#include "ServerBase.h"

namespace aiSocks {

// ---------------------------------------------------------------------------
// SimpleServer  convenience wrapper for TCP server polling loops.
//
// Inherits ServerBase<detail::NoClientState> (protected), so it shares the
// O(1) flat-array client table, keep-alive timeout heap, requestStop(), and
// signal handling.  Subclasses may override onClientConnected() and
// onClientDisconnected() to maintain a thread-safe atomic client count or
// any other per-connect/disconnect logic.
//
// Usage — poll loop (read + write events):
//   SimpleServer server(ServerBind{"0.0.0.0", Port{8080}});
//   if (!server.isValid()) { ... }
//   server.pollClients([](TcpSocket& client, PollEvent events) {
//       if (hasFlag(events, PollEvent::Readable)) {
//           char buf[1024];
//           int n = client.receive(buf, sizeof(buf));
//           if (n > 0) client.send(buf, n);
//       }
//       return true; // keep client connected
//   });
//
// Writable interest is off by default (same as ServerBase).  Call
// setClientWritable(sock, true) from inside the callback to opt a client
// into Writable events.
//
// Usage — accept-only loop (synchronous per-client callback, then close):
//   server.acceptClients([](TcpSocket& client) {
//       // do a blocking exchange; socket closes when this returns
//   });
//
// On failure (socket creation, bind, or listen), isValid() returns false.
// ---------------------------------------------------------------------------

namespace detail {
    // SimpleServer needs a ClientData type for ServerBase<T> but stores no
    // per-client state — all logic lives in the user's callback.  This tag
    // satisfies the template parameter without exposing implementation noise.
    struct NoClientState {};

    // Non-owning type-erased reference to any bool(TcpSocket&, PollEvent)
    // callable.  The pointed-to object must outlive all operator() calls.
    // Used instead of std::function to avoid heap allocation and <functional>.
    struct CallbackRef {
        void* obj{nullptr};
        bool (*invoke)(void*, TcpSocket&, PollEvent){nullptr};

        bool operator()(TcpSocket& s, PollEvent e) const {
            return invoke(obj, s, e);
        }
        explicit operator bool() const { return invoke != nullptr; }
    };
} // namespace detail

class SimpleServer : protected ServerBase<detail::NoClientState> {
    using Base = ServerBase<detail::NoClientState>;

    public:
    // Create a listening server socket.
    // Returns invalid server if bind or listen fails — check isValid().
    SimpleServer(
        const ServerBind& args, AddressFamily family = AddressFamily::IPv4)
        : Base(args, family) {}

    // Expose the useful subset of Base's public API.
    using Base::clientCount;
    using Base::getKeepAliveTimeout;
    using Base::getSocket;
    using Base::handlesSignals;
    using Base::isValid;
    using Base::peakClientCount;
    using Base::requestStop;
    using Base::setClientWritable;
    using Base::setHandleSignals;
    using Base::setKeepAliveTimeout;
    using Base::stopRequested;
    using Base::touchClient;

    // -------------------------------------------------------------------------
    // Full Poller-driven server loop: accept + readable + writable events.
    // Callback signature: bool(TcpSocket& client, PollEvent events)
    //
    // Return value from callback:
    //   true   keep the client registered.
    //   false  remove and close the client.
    //
    // maxClients / timeout have the same semantics as ServerBase::run().
    //
    // Note: PollEvent::Writable only fires for clients where you have called
    // setClientWritable(sock, true).  This avoids spurious callbacks when the
    // send buffer is always ready but you have nothing to write.
    // -------------------------------------------------------------------------
    template <typename Callback>
    void pollClients(Callback&& cb,
        ClientLimit maxClients = ClientLimit::Default,
        Milliseconds timeout = Milliseconds{-1}) {
        using RawType = std::remove_reference_t<Callback>;
        callback_ = {static_cast<void*>(&cb),
            [](void* p, TcpSocket& s, PollEvent e) -> bool {
                return (*static_cast<RawType*>(p))(s, e);
            }};
        Base::run(maxClients, timeout);
        callback_ = {};
    }

    // -------------------------------------------------------------------------
    // Accept-only loop: accepts up to maxClients connections, calls
    // onClient(TcpSocket&) synchronously for each, then closes the socket.
    //
    // Use pollClients() for servers that need to keep multiple clients alive
    // concurrently.  acceptClients() is for simple request/response servers
    // that handle one exchange per accepted socket.
    // -------------------------------------------------------------------------
    template <typename Callback>
    void acceptClients(
        Callback&& onClient, ClientLimit maxClients = ClientLimit::Default) {
        if (!isValid()) return;

        Poller poller;
        TcpSocket& listener = getSocket();
        if (!poller.add(listener, PollEvent::Readable | PollEvent::Error))
            return;

        size_t count = 0;
        while (maxClients == ClientLimit::Unlimited
            || count < static_cast<size_t>(maxClients)) {
            auto ready = poller.wait(Milliseconds{-1});
            for (const auto& event : ready) {
                if (event.socket != &listener) continue;
                if (!hasFlag(event.events, PollEvent::Readable)) continue;
                for (;;) {
                    auto client = listener.accept();
                    if (!client) break;
                    (void)client->setBlocking(false);
                    onClient(*client);
                    ++count;
                    if (maxClients != ClientLimit::Unlimited
                        && count >= static_cast<size_t>(maxClients))
                        return;
                }
            }
        }
    }

    protected:
    ServerResult onReadable(TcpSocket& sock, detail::NoClientState&) override {
        if (callback_ && !callback_(sock, PollEvent::Readable))
            return ServerResult::Disconnect;
        return ServerResult::KeepConnection;
    }

    ServerResult onWritable(TcpSocket& sock, detail::NoClientState&) override {
        if (callback_ && !callback_(sock, PollEvent::Writable))
            return ServerResult::Disconnect;
        return ServerResult::KeepConnection;
    }

    private:
    // Non-owning type-erased ref to the callback passed to pollClients().
    // Valid only during Base::run(); reset to {} immediately after.
    detail::CallbackRef callback_{};
};

} // namespace aiSocks

#endif // AISOCKS_SIMPLE_SERVER_H
