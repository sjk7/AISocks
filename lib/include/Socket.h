// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
#ifndef AISOCKS_SOCKET_H
#define AISOCKS_SOCKET_H

#include "Result.h"
#include "SocketTypes.h"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Platform-specific native handle types - opaque to prevent platform leaks
// ---------------------------------------------------------------------------
using NativeHandle = uintptr_t;
constexpr NativeHandle INVALID_NATIVE_HANDLE = 0;

// ---------------------------------------------------------------------------
// Span<T>  resolves to std::span on C++20; a minimal shim on C++17.
//
// Usage (same on both standards):
//   std::vector<std::byte> buf(1024);
//   socket.send(aiSocks::Span<const std::byte>{buf.data(), buf.size()});
//
// On C++20 you can also pass std::span<const std::byte> directly because the
// alias makes them the same type.
// ---------------------------------------------------------------------------
#if defined(__cpp_lib_span)
namespace aiSocks {
template<typename T>
using Span = std::span<T>;
}
#else
namespace aiSocks {
template<typename T>
class Span {
    T* ptr;
    std::size_t len;

public:
    constexpr Span() noexcept : ptr(nullptr), len(0) {}
    constexpr Span(T* p, std::size_t n) noexcept : ptr(p), len(n) {}
    
    template<typename Container>
    constexpr Span(Container& c) noexcept : ptr(c.data()), len(c.size()) {}
    
    template<typename Container>
    constexpr Span(const Container& c) noexcept : ptr(c.data()), len(c.size()) {}
    
    constexpr std::size_t size() const noexcept { return len; }
    constexpr bool empty() const noexcept { return len == 0; }

    constexpr T* begin() const noexcept { return ptr; }
    constexpr T* end() const noexcept { return ptr + len; }
    constexpr T& operator[](std::size_t i) const noexcept { return ptr[i]; }
    
    constexpr T* data() const noexcept { return ptr; }
};
} // namespace aiSocks
#endif

namespace aiSocks {

// Forward declarations
class SocketImpl;

// ---------------------------------------------------------------------------
// Socket  pImpl firewall base for TcpSocket and UdpSocket.
//
// All constructors, the destructor, and protocol-specific data-transfer
// methods are protected so that Socket cannot be instantiated or deleted
// through a base pointer directly.  Use TcpSocket or UdpSocket instead.
//
// All socket-option and query methods remain public so that code holding a
// reference to the base class can still call setReceiveTimeout(), setSendTimeout(), setNoDelay(), etc.
//
// The protected do*() bridge methods expose the underlying SocketImpl
// operations to derived classes without leaking SocketImpl.h into their
// headers.  Socket.cpp is the only file (besides TcpSocket.cpp for accept())
// that includes SocketImpl.h  the single firewall point.
// ---------------------------------------------------------------------------
class Socket {
    public:
    // -----------------------------------------------------------------
    // Public: socket options and query methods (shared by TCP + UDP)
    // -----------------------------------------------------------------

    // Blocking mode
    Result<void> setBlocking(bool blocking);
    bool isBlocking() const noexcept;

    // Block until the socket has readable data (or EOF) within the given
    // timeout.  Returns true if ready, false on timeout
    // (SocketError::Timeout) or select() failure (SocketError::Unknown).
    bool waitReadable(Milliseconds timeout);

    // Block until the socket send-buffer has space within the given timeout.
    bool waitWritable(Milliseconds timeout);

    // SO_REUSEADDR / SO_REUSEPORT
    Result<void> setReuseAddress(bool reuse);
    Result<void> setReusePort(bool enable);

    // Set SO_RCVTIMEO on the socket.
    //   defaultTimeout (30 s)  used when not specified.
    //   Milliseconds{0}        disables the timeout; recv() blocks
    //                           indefinitely until data arrives.
    //   any positive duration  recv() returns SocketError::Timeout after
    //                           waiting this long with no data.
    Result<void> setReceiveTimeout(Milliseconds timeout);

    // Set SO_SNDTIMEO on the socket (same semantics as setReceiveTimeout).
    Result<void> setSendTimeout(Milliseconds timeout);

    // Disable/enable Nagle's algorithm (TCP only).
    Result<void> setNoDelay(bool noDelay);
    bool getNoDelay() const;

    // Set the kernel receive / send socket buffer sizes (SO_RCVBUF /
    // SO_SNDBUF). `bytes` is the requested buffer size in bytes; the kernel
    // may round it up or clamp it. Returns false on setsockopt() failure.
    Result<void> setReceiveBufferSize(int bytes);
    Result<void> setSendBufferSize(int bytes);

    // Query the current kernel receive / send socket buffer sizes.
    // Returns -1 on getsockopt() failure.
    int getReceiveBufferSize() const;
    int getSendBufferSize() const;

    // UDP-only options (no-op on TCP).  Returns false if called on TCP.
    Result<void> setBroadcast(bool enable);
    Result<void> setMulticastTTL(int ttl);

    // TCP-only options (no-op on UDP).  Returns false if called on UDP.
    Result<void> setKeepAlive(bool enable);
    Result<void> setLingerAbort(bool enable);

    // Shutdown the socket (disable further sends/receives).
    bool shutdown(ShutdownHow how);

    // Utility
    void close() noexcept;
    bool isValid() const noexcept;
    AddressFamily getAddressFamily() const noexcept;
    SocketError getLastError() const noexcept;
    std::string getErrorMessage() const;

    // Query the local address/port assigned to this socket.
    Result<Endpoint> getLocalEndpoint() const;

    // Query the remote address/port this socket is connected to.
    Result<Endpoint> getPeerEndpoint() const;

    // Returns the underlying OS socket descriptor as a platform-specific handle.
    // Advanced use only (e.g. Poller integration).
    NativeHandle getNativeHandle() const noexcept;

    // Static utility methods
    static std::vector<NetworkInterface> getLocalAddresses();
    static bool isValidIPv4(const std::string& address);
    static bool isValidIPv6(const std::string& address);
    static std::string ipToString(const void* addr, AddressFamily family);

    // Prevent copying
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    protected:
    // -----------------------------------------------------------------
    // Protected: constructors / destructor / move
    // (only TcpSocket and UdpSocket may instantiate / destroy Socket)
    // -----------------------------------------------------------------

    // Basic constructor  creates the underlying socket fd.
    // Returns invalid socket if the OS call fails - check isValid().
    Socket(SocketType type, AddressFamily family);

    // Server socket  socket()  [SO_REUSEADDR]  bind()  listen().
    // Returns invalid socket if any step fails - check isValid().
    Socket(SocketType type, AddressFamily family, const ServerBind& config);

    // Client socket  socket()  connect().
    // Returns invalid socket if connection fails - check isValid().
    Socket(SocketType type, AddressFamily family, const ConnectArgs& config);

    // Takes ownership of an already-constructed impl (used by TcpSocket::accept()).
    explicit Socket(std::unique_ptr<SocketImpl> impl);

    // Non-virtual protected destructor: no vtable; delete-through-Socket* is a
    // compile-time error outside the hierarchy.
    ~Socket();

    // Allow moving by derived classes.
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;

    // -----------------------------------------------------------------
    // Protected: protocol-specific data-transfer methods
    // -----------------------------------------------------------------

    // TCP: send/receive exact byte counts.  UDP: send/receive datagrams.
    // Returns number of bytes transferred (>=0) or -1 on error.
    int doSend(const void* data, size_t length);
    int doReceive(void* buffer, size_t length);

    // Convenience overloads that work with Span<T>.
    int doSend(Span<const std::byte> data);
    int doReceive(Span<std::byte> buffer);

    // Send all bytes or fail (returns false on partial send or error).
    bool doSendAll(const void* data, size_t length);
    bool doSendAll(Span<const std::byte> data);

    // Receive all bytes or fail (returns false on EOF or partial receive).
    bool doReceiveAll(void* buffer, size_t length);
    bool doReceiveAll(Span<std::byte> buffer);

    // Send all with progress callback (returns false if callback returns <0).
    class SendProgressSink {
    public:
        virtual ~SendProgressSink() = default;
        virtual int operator()(size_t bytesSent, size_t totalBytes) = 0;
    };
    bool doSendAllProgress(const void* data, size_t length, SendProgressSink& progress);

    // UDP-only: send/receive to/from specific endpoint.
    int doSendTo(const void* data, size_t length, const Endpoint& remote);
    int doReceiveFrom(void* buffer, size_t length, Endpoint& remote);
    int doSendTo(Span<const std::byte> data, const Endpoint& remote);
    int doReceiveFrom(Span<std::byte> buffer, Endpoint& remote);

    // -----------------------------------------------------------------
    // Protected: socket setup helpers (used by derived classes)
    // -----------------------------------------------------------------

    // Bind to local address/port.  Returns false on error.
    bool doBind(const std::string& address, Port port);

    // Listen for incoming connections.  Returns false on error.
    bool doListen(int backlog = 10);

    // Accept an incoming connection.  Returns nullptr on error.
    std::unique_ptr<SocketImpl> doAccept();

    // Connect to remote address/port.  Returns false on error.
    bool doConnect(const std::string& address, Port port, Milliseconds timeout);

    private:
    std::unique_ptr<SocketImpl> pImpl;
};

} // namespace aiSocks

#endif // AISOCKS_SOCKET_H
