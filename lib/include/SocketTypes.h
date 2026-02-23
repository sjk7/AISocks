// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
#ifndef AISOCKS_SOCKET_TYPES_H
#define AISOCKS_SOCKET_TYPES_H

#include <cstdint>
#include <chrono>
#include <string>

namespace aiSocks {

// Lightweight milliseconds type to avoid including <chrono> in Socket.h.
// Provides implicit conversion from integers and std::chrono::duration types.
struct Milliseconds {
    int64_t count;

    constexpr Milliseconds() noexcept : count(0) {}
    constexpr explicit Milliseconds(int64_t ms) noexcept : count(ms) {}

    // Implicit conversion from std::chrono::duration types
    template<typename Rep, typename Period>
    constexpr Milliseconds(std::chrono::duration<Rep, Period> d) noexcept
        : count(std::chrono::duration_cast<std::chrono::milliseconds>(d).count()) {}

    constexpr int64_t milliseconds() const noexcept { return count; }

    constexpr bool operator==(Milliseconds other) const noexcept {
        return count == other.count;
    }
    constexpr bool operator!=(Milliseconds other) const noexcept {
        return count != other.count;
    }
    constexpr bool operator<(Milliseconds other) const noexcept {
        return count < other.count;
    }
    constexpr bool operator<=(Milliseconds other) const noexcept {
        return count <= other.count;
    }
    constexpr bool operator>(Milliseconds other) const noexcept {
        return count > other.count;
    }
    constexpr bool operator>=(Milliseconds other) const noexcept {
        return count >= other.count;
    }
};

// Default timeout applied to all optional timeout parameters.
inline constexpr Milliseconds defaultTimeout{30000}; // 30 seconds
inline constexpr Milliseconds defaultConnectTimeout{10000}; // 10 seconds

// Named timeout constants for common use cases.
namespace Timeouts {
    inline constexpr Milliseconds Immediate{0};
    inline constexpr Milliseconds Short{1000};
    inline constexpr Milliseconds Medium{5000};
    inline constexpr Milliseconds Long{30000};
}

enum class AddressFamily { IPv4, IPv6 };

enum class SocketType { TCP, UDP };

// Strong port-number type.  Accepts integer literals and named well-known
// ports interchangeably and converts back to uint16_t implicitly so all
// platform socket API calls (htons, etc.) require no casts.
//
// Usage:
//   Port p{8080};                    // from integer literal
//   Port p = Port{80};              // copy construction
//   uint16_t n = p;                // implicit conversion
//   socket.bind("0.0.0.0", 8080);   // implicit conversion
class Port {
    uint16_t value_;
public:
    constexpr Port() noexcept : value_(0) {}
    constexpr explicit Port(uint16_t value) noexcept : value_(value) {}
    
    // Allow implicit conversion from integer literals
    template<typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
    constexpr Port(T value) noexcept : value_(static_cast<uint16_t>(value)) {}
    
    constexpr uint16_t value() const noexcept { return value_; }
    constexpr operator uint16_t() const noexcept { return value_; }
    
    constexpr bool operator==(Port other) const noexcept { return value_ == other.value_; }
    constexpr bool operator!=(Port other) const noexcept { return value_ != other.value_; }
    constexpr bool operator<(Port other) const noexcept { return value_ < other.value_; }
    constexpr bool operator>(Port other) const noexcept { return value_ > other.value_; }
};

// Network endpoint: an (address, port, family) triple returned by
// getLocalEndpoint() and getPeerEndpoint(), and passed to sendTo().
struct Endpoint {
    std::string address; // dotted-decimal or colon-hex string
    Port port{0}; // port number
    AddressFamily family{}; // IPv4 or IPv6
    
    Endpoint() = default;
    Endpoint(const std::string& addr, Port p, AddressFamily fam)
        : address(addr), port(p), family(fam) {}
    
    bool operator==(const Endpoint& other) const noexcept {
        return address == other.address && port == other.port && family == other.family;
    }
    bool operator!=(const Endpoint& other) const noexcept {
        return !(*this == other);
    }
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

// Controls which direction shutdown() closes.
enum class ShutdownHow {
    Read, // discard queued input; peer SEND will get RST      (SHUT_RD)
    Write, // send FIN; peer recv will see EOF                  (SHUT_WR)
    Both, // both directions                                   (SHUT_RDWR)
};

struct NetworkInterface {
    std::string name; // Interface name (e.g., "eth0", "Ethernet")
    std::string address; // IP address
    AddressFamily family; // IPv4 or IPv6
    bool isLoopback; // True if loopback interface
};

// Creates a server socket: socket()  [SO_REUSEADDR]  bind()  listen()
// Returns invalid socket if any step fails - check isValid().
struct ServerBind {
    std::string address; // e.g. "0.0.0.0", "127.0.0.1", "::1"
    Port port{0};
    int backlog = 10;
    bool reuseAddr = true;
};

// Creates a connected client socket: socket()  connect()
// Returns invalid socket if connection fails - check isValid().
//
// connectTimeout controls how long to wait for the TCP handshake:
//   defaultTimeout (30 s)  used when not specified.
//   any positive duration  fails with SocketError::Timeout if not connected
//                           within that duration.
//   Milliseconds{0}        initiate the connect and return immediately with
//                           getLastError() == WouldBlock (connect in progress).
//                           The socket is left in whatever blocking mode it
//                           was in before the call (BlockingGuard restores it).
//                           For a Poller-driven async connect:
//                             1. Call setBlocking(false) on the socket first.
//                             2. Use connectTimeout = Milliseconds{0}.
//                             3. Expect WouldBlock  that is not an error.
//                             4. Register with a Poller (PollEvent::Writable).
//                             5. Call getPeerEndpoint() after writable fires
//                                to confirm success.
//
// Note: DNS resolution is synchronous and not covered by this timeout.
// Note: connect() is always issued on a non-blocking fd internally.
//       BlockingGuard saves the current OS blocking flag, sets O_NONBLOCK,
//       issues connect(), then restores the original flag on all exit paths.
struct ConnectArgs {
    std::string address; // Remote address or hostname
    Port port{0};
    Milliseconds connectTimeout{defaultConnectTimeout};
};

} // namespace aiSocks

#endif // AISOCKS_SOCKET_TYPES_H
