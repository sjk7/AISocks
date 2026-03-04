// This is an independent project of an individual developer. Dear PVS-Studio, please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com


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
    template <typename Rep, typename Period>
    constexpr Milliseconds(std::chrono::duration<Rep, Period> d) noexcept
        : count(std::chrono::duration_cast<std::chrono::milliseconds>(d)
                  .count()) {}

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

// Platform listen() backlog constants.
// All three OS values are always defined so the intent is readable everywhere.
// _WIN32 / __APPLE__ / __linux__ are predefined compiler macros, not headers.
struct Backlog {
    // macOS: SOMAXCONN = 128 in <sys/socket.h>.
    // The kernel clips to kern.ipc.somaxconn (default 128).
    // Raise with: sudo sysctl -w kern.ipc.somaxconn=4096
    static constexpr int somaxconnMacOS = 128;

    // Linux: SOMAXCONN = 128 in <sys/socket.h>.
    // The kernel clips to net.core.somaxconn (default 128 on kernels < 5.4,
    // raised to 4096 on kernels >= 5.4).
    // Raise with: sudo sysctl -w net.core.somaxconn=4096
    static constexpr int somaxconnLinux = 128;

    // Windows: SOMAXCONN = 0x7fffffff in <winsock2.h>.
    // This is a sentinel value — the TCP/IP stack picks the real cap (~200).
    // Passing any integer has the same effect; there is no user-tunable sysctl.
    static constexpr int somaxconnWindows = 0x7fff'ffff;

    // Maximum backlog for the current platform, selected at compile time.
#if defined(_WIN32)
    static constexpr int maxBacklog = somaxconnWindows;
#elif defined(__APPLE__)
    static constexpr int maxBacklog = somaxconnMacOS;
#else
    static constexpr int maxBacklog = somaxconnLinux;
#endif

    // Sensible default for general-purpose servers that do not need the
    // platform maximum (e.g. unit-test echo servers, port-availability probes).
    static constexpr int defaultBacklog = 64;

    // Current value. Defaults to defaultBacklog.
    // Use Backlog{4096} to request a higher queue after raising the sysctl in
    // *nix, or just Backlog{} for the default.
    int value = defaultBacklog;

    Backlog() = default;
    constexpr explicit Backlog(int v) noexcept : value(v) {}

    // Implicit conversion to int so POSIX/Winsock APIs need no call-site casts.
    constexpr operator int() const noexcept { return value; }
};

// Named timeout constants for common use cases.
namespace Timeouts {
    inline constexpr Milliseconds Immediate{0};
    inline constexpr Milliseconds Short{1000};
    inline constexpr Milliseconds Medium{5000};
    inline constexpr Milliseconds Long{30000};
} // namespace Timeouts

enum class AddressFamily { IPv4, IPv6 };

enum class SocketType { TCP, UDP };

// Strong port-number type.  Explicit construction only — passing a raw int
// or uint16_t where Port is expected is a compile error.  Use Port{8080} or
// Port{Port::http}.  Convert back with .value() or explicit cast.
//
// Usage:
//   Port p{8080};                        // OK — direct construction
//   Port p{Port::http};                  // OK — named constant
//   Port p = Port{80};                   // OK — copy construction
//   uint16_t n = p.value();             // OK — explicit accessor
//   uint16_t n = static_cast<uint16_t>(p); // OK — explicit cast
//   socket.bind("0.0.0.0", Port{Port::httpAlt}); // OK
//   socket.bind("0.0.0.0", 8080);       // ERROR — won't compile
class Port {
    uint16_t value_;

    public:
    constexpr Port() noexcept : value_(0) {}
    constexpr explicit Port(uint16_t value) noexcept : value_(value) {}

    constexpr uint16_t value() const noexcept { return value_; }
    constexpr explicit operator uint16_t() const noexcept { return value_; }

    constexpr bool operator==(Port other) const noexcept {
        return value_ == other.value_;
    }
    constexpr bool operator!=(Port other) const noexcept {
        return value_ != other.value_;
    }
    constexpr bool operator<(Port other) const noexcept {
        return value_ < other.value_;
    }
    constexpr bool operator>(Port other) const noexcept {
        return value_ > other.value_;
    }

    // -----------------------------------------------------------------------
    // Named well-known / common ports.
    // Usage:  Port::any          — let the OS assign an ephemeral port
    //         Port{Port::http}   — named port (explicit construction)
    //         Port{Port::httpAlt}  — alternate HTTP port
    // -----------------------------------------------------------------------

    // Let the OS assign an ephemeral port (bind to port 0).
    // Defined after the class body (Port must be complete for its own type).
    static const Port any;

    // File Transfer Protocol (control / data)
    static constexpr uint16_t ftp = 21;
    static constexpr uint16_t ftpData = 20;

    // Secure Shell
    static constexpr uint16_t ssh = 22;

    // Telnet
    static constexpr uint16_t telnet = 23;

    // Simple Mail Transfer Protocol
    static constexpr uint16_t smtp = 25;

    // Domain Name System
    static constexpr uint16_t dns = 53;

    // Hypertext Transfer Protocol
    static constexpr uint16_t http = 80;

    // Post Office Protocol v3
    static constexpr uint16_t pop3 = 110;

    // Internet Message Access Protocol
    static constexpr uint16_t imap = 143;

    // HTTPS (HTTP over TLS)
    static constexpr uint16_t https = 443;

    // SMTPS (SMTP over TLS)
    static constexpr uint16_t smtps = 465;

    // SMTP submission (STARTTLS)
    static constexpr uint16_t smtpSubmit = 587;

    // IMAPS (IMAP over TLS)
    static constexpr uint16_t imaps = 993;

    // POP3S (POP3 over TLS)
    static constexpr uint16_t pop3s = 995;

    // Common HTTP alternate / development ports
    static constexpr uint16_t httpAlt = 8080;
    static constexpr uint16_t httpsAlt = 8443;

    // Start of the IANA ephemeral (dynamic) port range
    static constexpr uint16_t ephemeralStart = 49152;
};

// Port::any is defined here (after class body) because Port must be complete.
inline const Port Port::any{};

// Network endpoint: an (address, port, family) triple returned by
// getLocalEndpoint() and getPeerEndpoint(), and passed to sendTo().
struct Endpoint {
    std::string address; // dotted-decimal or colon-hex string
    Port port{}; // port number
    AddressFamily family{}; // IPv4 or IPv6

    Endpoint() = default;
    Endpoint(const std::string& addr, Port p, AddressFamily fam)
        : address(addr), port(p), family(fam) {}

    bool operator==(const Endpoint& other) const noexcept {
        return address == other.address && port == other.port
            && family == other.family;
    }
    bool operator!=(const Endpoint& other) const noexcept {
        return !(*this == other);
    }

    // Format as "address:port" string for logging/display.
    std::string toString() const {
        return address + ":" + std::to_string(static_cast<uint16_t>(port));
    }

    // Check if this endpoint is a loopback address (127.x.x.x or ::1).
    bool isLoopback() const;

    // Check if this endpoint is on a private/reserved network
    // (10.x.x.x, 172.16-31.x.x, 192.168.x.x, fc00::/7, etc.).
    bool isPrivateNetwork() const;
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
    Port port{};
    Backlog backlog{};
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
    Port port{};
    Milliseconds connectTimeout{defaultConnectTimeout};
};

} // namespace aiSocks

#endif // AISOCKS_SOCKET_TYPES_H
