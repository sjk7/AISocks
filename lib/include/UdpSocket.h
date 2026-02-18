// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
#ifndef AISOCKS_UDP_SOCKET_H
#define AISOCKS_UDP_SOCKET_H

#include "Socket.h"

namespace aiSocks {

// ---------------------------------------------------------------------------
// UdpSocket — type-safe UDP socket.
//
// Inherits the Socket pImpl firewall.  Only UDP-meaningful operations are
// present; TCP-only operations (listen, accept, sendAll, receiveAll) are
// absent at compile time, preventing accidental misuse.
//
// Two usage modes:
//
//   Connectionless (typical):
//     UdpSocket s;
//     s.bind("0.0.0.0", Port{9000});       // to receive
//     Endpoint from;
//     s.receiveFrom(buf, len, from);
//     s.sendTo(reply, len, from);
//
//   Connected (fixed peer, simpler API):
//     UdpSocket s;
//     s.connect("192.168.1.5", Port{9000}); // select default destination
//     s.send(buf, len);                     // kernel fills in peer address
//     s.receive(buf, len);
//
// Destructor is public and non-virtual.  Socket's destructor is protected so
// delete-through-Socket* is a compile-time error outside the hierarchy.
// ---------------------------------------------------------------------------
class UdpSocket : public Socket {
    public:
    // Creates a UDP socket fd.
    // Throws SocketException(CreateFailed) if the OS call fails.
    explicit UdpSocket(AddressFamily family = AddressFamily::IPv4);

    // Public non-virtual destructor — chains to Socket::~Socket().
    ~UdpSocket() = default;

    // Move.
    UdpSocket(UdpSocket&&) noexcept = default;
    UdpSocket& operator=(UdpSocket&&) noexcept = default;

    // --- Bind (receiver side) ---

    // Bind to a local address/port to receive datagrams.
    // Required before receiveFrom() when acting as a server.
    [[nodiscard]] bool bind(const std::string& address, Port port) {
        return doBind(address, port);
    }

    // --- Connected-mode (optional) ---

    // Set a default destination.  Enables send() / receive() without an
    // explicit endpoint on every call.
    //
    // UDP "connect" is a purely local kernel operation — it records the peer
    // address so the kernel can filter incoming datagrams and fill in the
    // destination on outgoing ones.  No packets are sent; no handshake
    // occurs.  The call always completes instantly.  It cannot be polled.
    [[nodiscard]] bool connect(const std::string& address, Port port) {
        return doConnect(address, port, Milliseconds{0});
    }

    // --- Data transfer (connectionless) ---

    // Send a datagram to an explicit destination.
    int sendTo(const void* data, size_t length, const Endpoint& remote) {
        return doSendTo(data, length, remote);
    }
    int sendTo(Span<const std::byte> data, const Endpoint& remote) {
        return doSendTo(data, remote);
    }
    int receiveFrom(void* buffer, size_t length, Endpoint& remote) {
        return doReceiveFrom(buffer, length, remote);
    }
    int receiveFrom(Span<std::byte> buffer, Endpoint& remote) {
        return doReceiveFrom(buffer, remote);
    }

    // --- Data transfer (connected-mode) ---

    // Only valid after connect().
    int send(const void* data, size_t length) { return doSend(data, length); }
    int send(Span<const std::byte> data) { return doSend(data); }
    int receive(void* buffer, size_t length) {
        return doReceive(buffer, length);
    }
    int receive(Span<std::byte> buffer) { return doReceive(buffer); }

    // --- UDP-specific option ---

    // Enable / disable SO_BROADCAST.  Required before sending to a
    // limited-broadcast address (255.255.255.255 or subnet broadcast).
    bool setBroadcast(bool enable) { return doSetBroadcast(enable); }
};

} // namespace aiSocks

#endif // AISOCKS_UDP_SOCKET_H
