// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
#ifndef AISOCKS_TCP_SOCKET_H
#define AISOCKS_TCP_SOCKET_H

#include "Socket.h"
#include <memory>

namespace aiSocks {

// ---------------------------------------------------------------------------
// TcpSocket — type-safe TCP socket.
//
// Inherits the Socket pImpl firewall.  Only TCP-meaningful operations are
// present; UDP operations (sendTo, receiveFrom) are absent at compile time.
//
// Usage — server:
//   TcpSocket srv(AddressFamily::IPv4, ServerBind{"0.0.0.0", Port{8080}});
//   auto client = srv.accept();   // returns unique_ptr<TcpSocket>
//
// Usage — client:
//   TcpSocket c(AddressFamily::IPv4, ConnectTo{"example.com", Port{80}});
//   c.sendAll(buf, len);
//
// Destructor is public and non-virtual.  Socket's destructor is protected so
// delete-through-Socket* is a compile-time error outside the hierarchy.
// ---------------------------------------------------------------------------
class TcpSocket : public Socket {
    public:
    // Creates a bare TCP socket fd.
    // Throws SocketException(CreateFailed) if the OS call fails.
    explicit TcpSocket(AddressFamily family = AddressFamily::IPv4);

    // Server socket — socket() → [SO_REUSEADDR] → bind() → listen().
    // Throws SocketException on any step failure.
    TcpSocket(AddressFamily family, const ServerBind& cfg);

    // Client socket — socket() → connect().
    // Throws SocketException on any step failure.
    TcpSocket(AddressFamily family, const ConnectTo& cfg);

    // Public non-virtual destructor — chains to Socket::~Socket().
    ~TcpSocket() = default;

    // No copy.
    TcpSocket(const TcpSocket&) = delete;
    TcpSocket& operator=(const TcpSocket&) = delete;

    // Move.
    TcpSocket(TcpSocket&&) noexcept = default;
    TcpSocket& operator=(TcpSocket&&) noexcept = default;

    // --- Server operations ---
    [[nodiscard]] bool bind(const std::string& address, Port port) {
        return doBind(address, port);
    }
    [[nodiscard]] bool listen(int backlog = 128) { return doListen(backlog); }

    // Accept the next incoming connection.
    // Returns nullptr if accept() fails (check getLastError()).
    // Defined in TcpSocket.cpp (needs SocketImpl.h for the impl move).
    std::unique_ptr<TcpSocket> accept();

    // --- Client operation ---
    //   defaultTimeout (30 s) — wait for handshake.
    //   Milliseconds{0}       — non-blocking initiation (Poller-driven).
    //   any positive duration — fail with Timeout if not connected in time.
    bool connect(const std::string& address, Port port,
        Milliseconds timeout = defaultTimeout) {
        return doConnect(address, port, timeout);
    }

    // --- Data transfer ---
    //
    // Partial send / receive — may transfer fewer bytes than requested.
    // Returns bytes transferred, or <0 on error.
    int send(const void* data, size_t length) { return doSend(data, length); }
    int send(Span<const std::byte> data) { return doSend(data); }
    int receive(void* buffer, size_t length) {
        return doReceive(buffer, length);
    }
    int receive(Span<std::byte> buffer) { return doReceive(buffer); }

    // Loop until all bytes are sent / received, or error / EOF.
    // sendAll: returns false on error (check getLastError()).
    // receiveAll: returns false on error or peer-close before all bytes
    //   (getLastError() == ConnectionReset on clean EOF).
    bool sendAll(const void* data, size_t length) {
        return doSendAll(data, length);
    }
    bool sendAll(Span<const std::byte> data) { return doSendAll(data); }

    bool receiveAll(void* buffer, size_t length) {
        return doReceiveAll(buffer, length);
    }
    bool receiveAll(Span<std::byte> buffer) { return doReceiveAll(buffer); }

    // sendAll with a per-chunk progress callback.
    // `progress` is called after each successful write with the cumulative
    // bytes sent so far and the total requested.  On error the callback
    // reflects the last successfully sent offset.
    // Fn signature: void(size_t bytesSentSoFar, size_t total)
    template <typename Fn>
    bool sendAll(const void* data, size_t length, Fn&& progress) {
        const auto* ptr    = static_cast<const char*>(data);
        size_t      sent   = 0;
        while (sent < length) {
            int n = doSend(ptr + sent, length - sent);
            if (n <= 0) return false;
            sent += static_cast<size_t>(n);
            std::forward<Fn>(progress)(sent, length);
        }
        return true;
    }

    private:
    // Used by accept() to wrap an accepted SocketImpl.
    // Defined in TcpSocket.cpp.
    explicit TcpSocket(std::unique_ptr<SocketImpl> impl);
};

} // namespace aiSocks

#endif // AISOCKS_TCP_SOCKET_H
