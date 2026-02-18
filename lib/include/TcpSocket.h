// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
#ifndef AISOCKS_TCP_SOCKET_H
#define AISOCKS_TCP_SOCKET_H

#include "Socket.h"
#include <memory>

namespace aiSocks {

// ---------------------------------------------------------------------------
// TcpSocket  type-safe TCP socket.
//
// Inherits the Socket pImpl firewall.  Only TCP-meaningful operations are
// present; UDP operations (sendTo, receiveFrom) are absent at compile time.
//
// Usage  server:
//   TcpSocket srv(AddressFamily::IPv4, ServerBind{"0.0.0.0", Port{8080}});
//   auto client = srv.accept();   // returns unique_ptr<TcpSocket>
//
// Usage  client:
//   TcpSocket c(AddressFamily::IPv4, ConnectTo{"example.com", Port{80}});
//   c.sendAll(buf, len);
//
// Destructor is public and non-virtual.  Socket's destructor is protected so
// delete-through-Socket* is a compile-time error outside the hierarchy.
// ---------------------------------------------------------------------------
class TcpSocket : public Socket {
    public:
    // Server socket  socket()  [SO_REUSEADDR]  bind()  listen().
    // Throws SocketException on any step failure.
    TcpSocket(AddressFamily family, const ServerBind& cfg);

    // Client socket  socket()  connect().
    // Throws SocketException on any step failure.
    TcpSocket(AddressFamily family, const ConnectTo& cfg);

    // Public non-virtual destructor  chains to Socket::~Socket().
    ~TcpSocket() = default;

    // Move.
    TcpSocket(TcpSocket&&) noexcept = default;
    TcpSocket& operator=(TcpSocket&&) noexcept = default;

    // Creates a raw, unbound, unconnected TCP socket fd.
    //
    // Prefer the ServerBind / ConnectTo constructors  they construct a
    // fully-ready socket in one step and uphold the correct-by-construction
    // invariant.  Use createRaw() only when you need an empty socket to test
    // socket options, error codes, or move semantics in isolation.
    //
    // Returns an invalid socket (isValid() == false) instead of throwing if
    // the OS call fails.
    static TcpSocket createRaw(AddressFamily family = AddressFamily::IPv4);

    // --- Server operations ---
    [[nodiscard]] bool bind(const std::string& address, Port port) {
        return doBind(address, port);
    }
    [[nodiscard]] bool listen(int backlog = 128) { return doListen(backlog); }

    // Accept the next incoming connection.
    // Returns nullptr if accept() fails (check getLastError()).
    // Defined in TcpSocket.cpp (needs SocketImpl.h for the impl move).
    [[nodiscard]] std::unique_ptr<TcpSocket> accept();

    // --- Client operation ---
    // Blocking connect: waits for the TCP handshake and returns true on success.
    //   defaultTimeout (30 s)  used when not specified.
    //   any positive duration  fail with Timeout if not connected in time.
    //
    // For Poller-driven (non-blocking) connect:
    //   call setBlocking(false) first, then connect(..., Milliseconds{0}).
    //   WouldBlock is returned  that is not an error; it means the handshake
    //   is in progress.  BlockingGuard inside SocketImpl::connect() saves the
    //   OS non-blocking flag and restores it on all exit paths.
    [[nodiscard]] bool connect(const std::string& address, Port port,
        Milliseconds timeout = defaultTimeout) {
        return doConnect(address, port, timeout);
    }

    // --- Data transfer ---
    //
    // Partial send / receive  may transfer fewer bytes than requested.
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
    //
    // `progress` is called after each successful write chunk with:
    //   bytesSentSoFar  cumulative bytes delivered so far
    //   total           total bytes requested
    //
    // Return value from the callback:
    //   >= 0  continue sending
    //   <  0  cancel immediately; sendAll() returns false with
    //          getLastError() == SocketError::None (distinguishes
    //          user cancellation from a genuine send error)
    //
    // Any callable is accepted (lambda with captures, functor, etc.).
    // The Adapter is stack-local  no heap allocation.
    template <typename Fn>
    bool sendAll(const void* data, size_t length, Fn&& progress) {
        struct Adapter : SendProgressSink {
            Fn& fn_;
            explicit Adapter(Fn& f) noexcept : fn_(f) {}
            int operator()(size_t s, size_t t) override { return fn_(s, t); }
        } adapter{progress};
        return doSendAllProgress(data, length, adapter);
    }

    private:
    // Raw socket without bind/connect.  Private so users cannot
    // accidentally hold an unconnected socket.  Use createRaw() instead.
    explicit TcpSocket(AddressFamily family = AddressFamily::IPv4);

    // Used by accept() to wrap an accepted SocketImpl.
    // Defined in TcpSocket.cpp.
    explicit TcpSocket(std::unique_ptr<SocketImpl> impl);
};

} // namespace aiSocks

#endif // AISOCKS_TCP_SOCKET_H
