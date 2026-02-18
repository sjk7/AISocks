// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
#ifndef AISOCKS_SOCKET_H
#define AISOCKS_SOCKET_H

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Span<T> — resolves to std::span on C++20; a minimal shim on C++17.
//
// Usage (same on both standards):
//   std::vector<std::byte> buf(1024);
//   socket.send(aiSocks::Span<const std::byte>{buf.data(), buf.size()});
//
// On C++20 you can also pass std::span<const std::byte> directly because the
// alias makes them the same type.
// ---------------------------------------------------------------------------
#if defined(__cpp_lib_span)
#include <span>
namespace aiSocks {
template <typename T> using Span = std::span<T>;
}
#else
namespace aiSocks {
/// Minimal contiguous-range view.  API subset of std::span<T>.
template <typename T> struct Span {
    T* ptr;
    std::size_t len;

    constexpr Span() noexcept : ptr(nullptr), len(0) {}
    constexpr Span(T* p, std::size_t n) noexcept : ptr(p), len(n) {}

    constexpr T* data() const noexcept { return ptr; }
    constexpr std::size_t size() const noexcept { return len; }
    constexpr bool empty() const noexcept { return len == 0; }

    constexpr T* begin() const noexcept { return ptr; }
    constexpr T* end() const noexcept { return ptr + len; }
    constexpr T& operator[](std::size_t i) const noexcept { return ptr[i]; }
};
} // namespace aiSocks
#endif

namespace aiSocks {

// Convenience alias for all timeout parameters in the public API.
// Any std::chrono::duration that converts to milliseconds (e.g.
// std::chrono::seconds, std::chrono::milliseconds) is accepted implicitly.
using Milliseconds = std::chrono::milliseconds;

// Default timeout applied to all optional timeout parameters.
inline constexpr Milliseconds defaultTimeout{std::chrono::seconds{30}};

// Strong port-number type.  Accepts integer literals and named well-known
// ports interchangeably and converts back to uint16_t implicitly so all
// platform socket API calls (htons, etc.) require no casts.
//
// Usage:
//   Port p{8080};                    // from integer literal
//   Port p = Port::Known::HTTPS;     // named constant
//   ServerBind{ .port = Port::Known::HTTP };   // in config structs
struct Port {
    enum class Known : uint16_t {
        FTP_DATA = 20,
        FTP = 21,
        SSH = 22,
        TELNET = 23,
        SMTP = 25,
        DNS = 53,
        HTTP = 80,
        POP3 = 110,
        IMAP = 143,
        HTTPS = 443,
        SMTPS = 465,
        IMAPS = 993,
        POP3S = 995,
        MQTT = 1883,
        HTTP_ALT = 8080,
        MQTTS = 8883,
    };

    uint16_t value{0};

    constexpr Port() noexcept = default;
    constexpr explicit Port(uint16_t v) noexcept : value(v) {}
    constexpr explicit Port(int v) noexcept : value(static_cast<uint16_t>(v)) {}
    constexpr Port(Known k) noexcept : value(static_cast<uint16_t>(k)) {}

    constexpr operator uint16_t() const noexcept { return value; }

    constexpr bool operator==(Port other) const noexcept {
        return value == other.value;
    }
    constexpr bool operator!=(Port other) const noexcept {
        return value != other.value;
    }
};

enum class AddressFamily { IPv4, IPv6 };

// Network endpoint: an (address, port, family) triple returned by
// getLocalEndpoint() and getPeerEndpoint(), and passed to sendTo().
struct Endpoint {
    std::string address; // dotted-decimal or colon-hex string
    Port port{0}; // port number
    AddressFamily family{}; // IPv4 or IPv6

    // Convenience: "addr:port" (IPv4) or "[addr]:port" (IPv6) string for
    // logging.  The bracketed form is required for IPv6 so the port is
    // unambiguous (RFC 2732 / the URI standard).
    std::string toString() const {
        if (family == AddressFamily::IPv6)
            return "[" + address + "]:" + std::to_string(port.value);
        return address + ":" + std::to_string(port.value);
    }
};

// Controls which direction shutdown() closes.
enum class ShutdownHow {
    Read, // discard queued input; peer SEND will get RST      (SHUT_RD)
    Write, // send FIN; peer recv will see EOF                  (SHUT_WR)
    Both, // both directions                                   (SHUT_RDWR)
};

class SocketImpl;

enum class SocketType { TCP, UDP };

struct NetworkInterface {
    std::string name; // Interface name (e.g., "eth0", "Ethernet")
    std::string address; // IP address as string
    AddressFamily family; // IPv4 or IPv6
    bool isLoopback; // True if loopback interface
};

enum class SocketError {
    None,
    CreateFailed,
    BindFailed,
    ListenFailed,
    AcceptFailed,
    ConnectFailed,
    SendFailed,
    ReceiveFailed,
    ConnectionReset, // peer closed / ECONNRESET / EPIPE / WSAECONNRESET
    SetOptionFailed,
    InvalidSocket,
    Timeout,
    WouldBlock,
    Unknown
};

// Thrown only from constructors when socket setup cannot be completed.
// Ingredients are captured eagerly at throw time; the formatted string is
// built once, on the first call to what(), and cached.
// Format: "<step>: <description> [<sysCode>: <system text>]"
class SocketException : public std::exception {
    public:
    SocketException(SocketError code, std::string step, std::string description,
        int sysCode, bool isDns)
        : errorCode_(code)
        , step_(std::move(step))
        , description_(std::move(description))
        , sysCode_(sysCode)
        , isDns_(isDns) {}

    SocketError errorCode() const noexcept { return errorCode_; }
    const char* what() const noexcept override; // defined in Socket.cpp

    private:
    SocketError errorCode_;
    std::string step_;
    std::string description_; // SocketImpl step description
    int sysCode_{0}; // errno / WSAGetLastError / EAI_*
    bool isDns_{false}; // true → translate with gai_strerror
    mutable std::string whatCache_;
};

// -----------------------------------------------------------------------
// Configuration structs for correct-by-construction sockets.
// Pass one of these to the Socket constructor instead of calling
// bind/listen/connect manually.
// -----------------------------------------------------------------------

// Creates a server socket: socket() → [SO_REUSEADDR] → bind() → listen()
// Throws SocketException with context if any step fails.
struct ServerBind {
    std::string address; // e.g. "0.0.0.0", "127.0.0.1", "::1"
    Port port{0};
    int backlog = 10;
    bool reuseAddr = true;
};

// Creates a connected client socket: socket() → connect()
// Throws SocketException with context if any step fails.
//
// connectTimeout controls how long to wait for the TCP handshake:
//   defaultTimeout (30 s) — used when not specified.
//   any positive duration — throw SocketException(Timeout) if not connected
//                           within that duration.
//   Milliseconds{0}       — initiate the connect and return immediately with
//                           getLastError() == WouldBlock (connect in progress).
//                           The socket is left in whatever blocking mode it
//                           was in before the call (BlockingGuard restores it).
//                           For a Poller-driven async connect:
//                             1. Call setBlocking(false) on the socket first.
//                             2. Use connectTimeout = Milliseconds{0}.
//                             3. Expect WouldBlock — that is not an error.
//                             4. Register with a Poller (PollEvent::Writable).
//                             5. Call getPeerEndpoint() after writable fires
//                                to confirm success.
//
// Note: DNS resolution is synchronous and not covered by this timeout.
// Note: connect() is always issued on a non-blocking fd internally.
//       BlockingGuard saves the current OS blocking flag, sets O_NONBLOCK,
//       issues connect(), then restores the original flag on all exit paths.
struct ConnectTo {
    std::string address; // Remote address or hostname
    Port port{0};
    Milliseconds connectTimeout{defaultTimeout};
};

// ---------------------------------------------------------------------------
// Socket — pImpl firewall base for TcpSocket and UdpSocket.
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
// that includes SocketImpl.h — the single firewall point.
// ---------------------------------------------------------------------------
class Socket {
    public:
    // -----------------------------------------------------------------
    // Public: socket options and query methods (shared by TCP + UDP)
    // -----------------------------------------------------------------

    // Blocking mode
    [[nodiscard]] bool setBlocking(bool blocking);
    bool isBlocking() const noexcept;

    // Block until the socket has readable data (or EOF) within the given
    // timeout.  Returns true if ready, false on timeout
    // (SocketError::Timeout) or select() failure (SocketError::Unknown).
    bool waitReadable(Milliseconds timeout);

    // Block until the socket send-buffer has space within the given timeout.
    bool waitWritable(Milliseconds timeout);

    // SO_REUSEADDR / SO_REUSEPORT
    bool setReuseAddress(bool reuse);
    bool setReusePort(bool enable);

    // Set SO_RCVTIMEO on the socket.
    //   defaultTimeout (30 s) — used when not specified.
    //   Milliseconds{0}       — disables the timeout; recv() blocks
    //                           indefinitely until data arrives.
    //   any positive duration — recv() returns SocketError::Timeout after
    //                           waiting this long with no data.
    bool setReceiveTimeout(Milliseconds timeout);

    // Set SO_SNDTIMEO on the socket (same semantics as setReceiveTimeout).
    bool setSendTimeout(Milliseconds timeout);

    // Disable/enable Nagle's algorithm (TCP only).
    // setNoDelay(true) reduces latency for small writes at the cost of
    // increased packet count.
    bool setNoDelay(bool noDelay);

    // Set the kernel receive / send socket buffer sizes (SO_RCVBUF /
    // SO_SNDBUF). `bytes` is the requested buffer size in bytes; the kernel
    // may round it up or clamp it. Returns false on setsockopt() failure.
    bool setReceiveBufferSize(int bytes);
    bool setSendBufferSize(int bytes);

    // Enable/disable SO_KEEPALIVE.
    bool setKeepAlive(bool enable);

    // Half-close the connection in the specified direction.
    bool shutdown(ShutdownHow how);

    // Configure SO_LINGER with l_linger=0: close() sends RST instead of FIN.
    bool setLingerAbort(bool enable);

    // Utility
    void close() noexcept;
    bool isValid() const noexcept;
    AddressFamily getAddressFamily() const noexcept;
    SocketError getLastError() const noexcept;
    std::string getErrorMessage() const;

    // Query the local address/port assigned to this socket.
    std::optional<Endpoint> getLocalEndpoint() const;

    // Query the remote address/port this socket is connected to.
    std::optional<Endpoint> getPeerEndpoint() const;

    // Returns the underlying OS socket descriptor as an opaque integer.
    // Advanced use only (e.g. Poller integration).
    uintptr_t getNativeHandle() const noexcept;

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

    // Basic constructor — creates the underlying socket fd.
    // Throws SocketException(SocketError::CreateFailed, ...) if the OS call
    // fails.
    Socket(SocketType type, AddressFamily family);

    // Server socket — socket() → [SO_REUSEADDR] → bind() → listen().
    // Throws SocketException (with the failing step prepended) on any failure.
    Socket(SocketType type, AddressFamily family, const ServerBind& config);

    // Client socket — socket() → connect().
    // Throws SocketException (with the failing step prepended) on any failure.
    Socket(SocketType type, AddressFamily family, const ConnectTo& config);

    // Takes ownership of an already-constructed impl (used by TcpSocket::accept()).
    explicit Socket(std::unique_ptr<SocketImpl> impl);

    // Non-virtual protected destructor: no vtable; delete-through-Socket* is a
    // compile-time error outside the hierarchy.
    ~Socket();

    // Allow moving by derived classes.
    Socket(Socket&&) noexcept;
    Socket& operator=(Socket&&) noexcept;

    // -----------------------------------------------------------------
    // Protected: protocol bridge methods (do* prefix)
    // Bodies live in Socket.cpp — the single SocketImpl.h include point.
    // -----------------------------------------------------------------

    // Server operations
    [[nodiscard]] bool doBind(const std::string& address, Port port);
    [[nodiscard]] bool doListen(int backlog = 10);

    // Returns the accepted SocketImpl, or nullptr on failure.
    // TcpSocket::accept() wraps this in a new TcpSocket.
    std::unique_ptr<SocketImpl> doAccept();

    // Client operation
    //
    // timeout controls how long to wait for the TCP handshake:
    //   defaultTimeout (30 s) — used when not specified.
    //   any positive duration — fail with SocketError::Timeout if not connected
    //                           within that duration.
    //   Milliseconds{0}       — initiate connect and return WouldBlock
    //                           immediately; call setBlocking(false) first so
    //                           BlockingGuard saves & restores non-blocking mode.
    bool doConnect(const std::string& address, Port port,
        Milliseconds timeout = defaultTimeout);

    // Data transfer (raw pointer overloads)
    int doSend(const void* data, size_t length);
    int doReceive(void* buffer, size_t length);

    // Send all bytes, looping until every byte is delivered or an error occurs.
    bool doSendAll(const void* data, size_t length);
    bool doSendAll(Span<const std::byte> data);

    // Receive exactly `length` bytes; returns false on error or EOF.
    bool doReceiveAll(void* buffer, size_t length);
    bool doReceiveAll(Span<std::byte> buffer);

    // Span overloads
    int doSend(Span<const std::byte> data);
    int doReceive(Span<std::byte> buffer);

    // Type-erased progress sink used by doSendAllProgress().
    // operator() is called after each successful send chunk with the
    // cumulative bytes sent so far and the total requested.
    // Return 0 to continue; return any negative value to cancel the
    // transfer immediately (doSendAllProgress returns false with
    // SocketError::None — the caller distinguishes cancel from error
    // by checking getLastError()).
    struct SendProgressSink {
        virtual int operator()(size_t bytesSentSoFar, size_t total) = 0;
        virtual ~SendProgressSink() = default;
    };

    // Loop body lives in Socket.cpp behind the pImpl firewall.
    // The TcpSocket::sendAll<Fn> template builds a stack-local Adapter
    // that wraps any callable into a SendProgressSink, so callers use
    // lambdas with captures at zero allocation cost.
    bool doSendAllProgress(
        const void* data, size_t length, SendProgressSink& progress);

    // UDP datagram transfer
    int doSendTo(const void* data, size_t length, const Endpoint& remote);
    int doReceiveFrom(void* buffer, size_t length, Endpoint& remote);
    int doSendTo(Span<const std::byte> data, const Endpoint& remote);
    int doReceiveFrom(Span<std::byte> buffer, Endpoint& remote);

    // UDP SO_BROADCAST
    bool doSetBroadcast(bool enable);

    private:
    std::unique_ptr<SocketImpl> pImpl;
};

} // namespace aiSocks

#endif // AISOCKS_SOCKET_H
