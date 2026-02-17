#ifndef AISOCKS_SOCKET_H
#define AISOCKS_SOCKET_H

#include <string>
#include <memory>
#include <cstdint>
#include <vector>

namespace aiSocks {

class SocketImpl;

enum class SocketType {
    TCP,
    UDP
};

enum class AddressFamily {
    IPv4,
    IPv6
};

struct NetworkInterface {
    std::string name;              // Interface name (e.g., "eth0", "Ethernet")
    std::string address;           // IP address as string
    AddressFamily family;          // IPv4 or IPv6
    bool isLoopback;              // True if loopback interface
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

class Socket {
public:
    Socket(SocketType type = SocketType::TCP, AddressFamily family = AddressFamily::IPv4);
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
