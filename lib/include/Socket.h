#ifndef AISOCKS_SOCKET_H
#define AISOCKS_SOCKET_H

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace aiSocks {

// Convenience alias for all timeout parameters in the public API.
// Any std::chrono::duration that converts to milliseconds (e.g.
// std::chrono::seconds, std::chrono::milliseconds) is accepted implicitly.
using Milliseconds = std::chrono::milliseconds;

// Default timeout applied to all optional timeout parameters.
// Passing Milliseconds{0} explicitly to any timeout parameter overrides this
// and tells the library to defer entirely to the OS (blocking until the kernel
// gives up, which can be several minutes on a dropped-SYN connection).
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

    // Convenience: "addr:port" string for logging.
    std::string toString() const {
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
    CloseFailed,
    SetOptionFailed,
    InvalidSocket,
    Timeout,
    WouldBlock,
    Unknown
};

// Thrown only from constructors when socket setup cannot be completed.
// what() returns the full context string prepended to the system error
// description.
class SocketException : public std::runtime_error {
    public:
    SocketException(SocketError code, const std::string& message)
        : std::runtime_error(message), errorCode_(code) {}

    SocketError errorCode() const noexcept { return errorCode_; }

    private:
    SocketError errorCode_;
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
//   Milliseconds{0}       — defer to the OS; blocks until the kernel gives up
//                           (can be several minutes on a silent SYN-drop).
//   any positive duration — throw SocketException(Timeout) if not connected
//                           within that duration.
//
// Note: DNS resolution is synchronous and not covered by this timeout.
struct ConnectTo {
    std::string address; // Remote address or hostname
    Port port{0};
    Milliseconds connectTimeout{defaultTimeout}; // see above
};

class Socket {
    public:
    // Basic constructor – creates the underlying socket fd.
    // Throws SocketException(SocketError::CreateFailed, ...) if the OS call
    // fails.
    Socket(SocketType type = SocketType::TCP,
        AddressFamily family = AddressFamily::IPv4);

    // Server socket – socket() → [SO_REUSEADDR] → bind() → listen().
    // Throws SocketException (with the failing step prepended) on any failure.
    Socket(SocketType type, AddressFamily family, const ServerBind& config);

    // Client socket – socket() → connect().
    // Throws SocketException (with the failing step prepended) on any failure.
    Socket(SocketType type, AddressFamily family, const ConnectTo& config);

    virtual ~Socket();

    // Prevent copying
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    // Allow moving
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;

    // Server operations
    bool bind(const std::string& address, Port port);
    bool listen(int backlog = 10);
    std::unique_ptr<Socket> accept();

    // Client operations
    bool connect(const std::string& address, Port port);

    // Data transfer
    int send(const void* data, size_t length);
    int receive(void* buffer, size_t length);

    // UDP-only data transfer (connected or connectionless)
    // sendTo: sends a datagram to the specified remote endpoint.
    // receiveFrom: receives a datagram and fills `remote` with the sender.
    int sendTo(const void* data, size_t length, const Endpoint& remote);
    int receiveFrom(void* buffer, size_t length, Endpoint& remote);

    // Socket options
    bool setBlocking(bool blocking);
    bool isBlocking() const;
    bool setReuseAddress(bool reuse);

    // Set SO_RCVTIMEO on the socket.
    //   defaultTimeout (30 s) — used when not specified.
    //   Milliseconds{0}       — disables the timeout; recv() blocks
    //                           indefinitely until data arrives.
    //   any positive duration — recv() returns SocketError::Timeout after
    //                           waiting this long with no data.
    bool setTimeout(Milliseconds timeout);

    // Set SO_SNDTIMEO on the socket (same semantics as setTimeout).
    bool setSendTimeout(Milliseconds timeout);

    // Disable/enable Nagle's algorithm (TCP only).
    // setNoDelay(true) reduces latency for small writes at the cost of
    // increased packet count.
    bool setNoDelay(bool noDelay);

    // Enable/disable SO_KEEPALIVE.  Requires OS idle/interval/count tuning
    // for meaningful control; enabling the option is still useful to detect
    // dead peers on long-lived connections.
    bool setKeepAlive(bool enable);

    // Half-close the connection in the specified direction.
    // Unlike close(), the socket fd remains valid after shutdown().
    bool shutdown(ShutdownHow how);

    // Utility
    void close();
    bool isValid() const;
    AddressFamily getAddressFamily() const;
    SocketError getLastError() const;
    std::string getErrorMessage() const;

    // Query the local address/port assigned to this socket
    // (populated after bind() or connect()).
    // Returns std::nullopt if the socket is invalid or getsockname fails.
    std::optional<Endpoint> getLocalEndpoint() const;

    // Query the remote address/port this socket is connected to.
    // Returns std::nullopt if not connected or the socket is invalid.
    std::optional<Endpoint> getPeerEndpoint() const;

    // Static utility methods
    static std::vector<NetworkInterface> getLocalAddresses();
    static bool isValidIPv4(const std::string& address);
    static bool isValidIPv6(const std::string& address);
    static std::string ipToString(const void* addr, AddressFamily family);

    private:
    // Private constructor for accepted connections
    Socket(std::unique_ptr<SocketImpl> impl);

    std::unique_ptr<SocketImpl> pImpl;
};

} // namespace aiSocks

#endif // AISOCKS_SOCKET_H
