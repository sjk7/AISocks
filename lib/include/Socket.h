#ifndef AISOCKS_SOCKET_H
#define AISOCKS_SOCKET_H

#include <string>
#include <memory>
#include <cstdint>
#include <vector>
#include <stdexcept>

namespace aiSocks {

class SocketImpl;

enum class SocketType { TCP, UDP };

enum class AddressFamily { IPv4, IPv6 };

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
    uint16_t port = 0;
    int backlog = 10;
    bool reuseAddr = true;
};

// Creates a connected client socket: socket() → connect()
// Throws SocketException with context if any step fails.
// connectTimeoutMs: 0 = OS default (blocks until kernel gives up).
//                  >0 = throw SocketException(Timeout) if not connected within
//                       this many milliseconds (covers DNS + TCP handshake).
struct ConnectTo {
    std::string address; // Remote address or hostname
    uint16_t port = 0;
    int connectTimeoutMs = 0;
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

    ~Socket();

    // Prevent copying
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    // Allow moving
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;

    // Server operations
    bool bind(const std::string& address, uint16_t port);
    bool listen(int backlog = 10);
    std::unique_ptr<Socket> accept();

    // Client operations
    bool connect(const std::string& address, uint16_t port);

    // Data transfer
    int send(const void* data, size_t length);
    int receive(void* buffer, size_t length);

    // Socket options
    bool setBlocking(bool blocking);
    bool isBlocking() const;
    bool setReuseAddress(bool reuse);
    bool setTimeout(int seconds);

    // Utility
    void close();
    bool isValid() const;
    AddressFamily getAddressFamily() const;
    SocketError getLastError() const;
    std::string getErrorMessage() const;

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
