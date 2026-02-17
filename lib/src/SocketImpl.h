// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
#ifndef AISOCKS_SOCKET_IMPL_H
#define AISOCKS_SOCKET_IMPL_H

#include "Socket.h"
#include <chrono>
#include <optional>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
using SocketHandle = SOCKET;
#define INVALID_SOCKET_HANDLE INVALID_SOCKET
#define SOCKET_ERROR_CODE SOCKET_ERROR
#else
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <errno.h>
#include <ifaddrs.h>
#include <netinet/tcp.h>
using SocketHandle = int;
#define INVALID_SOCKET_HANDLE -1
#define SOCKET_ERROR_CODE -1
#endif

namespace aiSocks {

// Internal: raw ingredients captured at the point of failure.
// The human-readable string is produced lazily via formatErrorContext().
struct ErrorContext {
    std::string description; // step description from SocketImpl
    int sysCode{0}; // errno / WSAGetLastError / EAI_*
    bool isDns{false}; // true → translate with gai_strerror
};

class SocketImpl {
    public:
    SocketImpl(SocketType type, AddressFamily family);
    ~SocketImpl();

    // Platform initialization/cleanup
    static bool platformInit();
    static void platformCleanup();

    // Static utility methods
    static std::vector<NetworkInterface> getLocalAddresses();
    static bool isValidIPv4(const std::string& address);
    static bool isValidIPv6(const std::string& address);
    static std::string ipToString(const void* addr, AddressFamily family);

    // Server operations
    bool bind(const std::string& address, Port port);
    bool listen(int backlog);
    std::unique_ptr<SocketImpl> accept();

    // Client operations
    // timeout == 0: blocking (OS default). >0: fail with Timeout if the
    // TCP handshake takes longer than timeout (DNS resolution is not covered).
    bool connect(const std::string& address, Port port,
        std::chrono::milliseconds timeout = defaultTimeout);

    // Data transfer
    int send(const void* data, size_t length);
    int receive(void* buffer, size_t length);
    int sendTo(const void* data, size_t length, const Endpoint& remote);
    int receiveFrom(void* buffer, size_t length, Endpoint& remote);

    // Socket options
    bool setBlocking(bool blocking);
    bool isBlocking() const;
    bool setReuseAddress(bool reuse);
    bool setReusePort(bool enable);
    bool setTimeout(std::chrono::milliseconds timeout);
    bool setSendTimeout(std::chrono::milliseconds timeout);
    bool setNoDelay(bool noDelay);
    bool setKeepAlive(bool enable);
    bool setLingerAbort(bool enable);
    bool setReceiveBufferSize(int bytes);
    bool setSendBufferSize(int bytes);
    bool shutdown(ShutdownHow how);
    bool sendAll(const void* data, size_t length);
    bool waitReadable(std::chrono::milliseconds timeout);
    bool waitWritable(std::chrono::milliseconds timeout);

    // Utility
    void close();
    bool isValid() const;
    AddressFamily getAddressFamily() const;
    SocketError getLastError() const;
    std::string getErrorMessage() const;
    ErrorContext getErrorContext() const;
    std::optional<Endpoint> getLocalEndpoint() const;
    std::optional<Endpoint> getPeerEndpoint() const;
    SocketHandle getRawHandle() const noexcept { return socketHandle; }

    // Constructor for accepted connections (public for make_unique)
    SocketImpl(SocketHandle handle, SocketType type, AddressFamily family);

    private:
    SocketHandle socketHandle;
    SocketType socketType;
    AddressFamily addressFamily;
    SocketError lastError{SocketError::None};

    // Error-state components.
    // lastSysCode is captured eagerly (errno / WSAGetLastError() is a
    // thread-local overwritten by the next syscall).  The string translation
    // (strerror / gai_strerror / FormatMessage) is a pure int→string lookup
    // that is stable over time, so it is deferred to getErrorMessage().
    std::string lastErrorDesc; // step description
    int lastSysCode{0}; // errno / WSAGetLastError / EAI_*
    bool lastErrorIsDns{false}; // true -> translate with gai_strerror

    // Lazy cache: built on first call to getErrorMessage() after each error.
    mutable std::string lastErrorMessage;
    mutable bool errorMessageDirty{false};

    bool blockingMode{true};
    bool shutdownCalled_{false}; // true after user calls shutdown(); close()
                                 // skips redundant ::shutdown()

    // Standard setter: reads errno / WSAGetLastError() immediately.
    void setError(SocketError error, const std::string& description);

    // DNS variant: stores a getaddrinfo EAI_* code; getErrorMessage() will
    // translate it with gai_strerror (POSIX) rather than
    // strerror/FormatMessage.
    void setErrorDns(
        SocketError error, const std::string& description, int gaiCode);

    int getLastSystemError() const;
    static Endpoint endpointFromSockaddr(const sockaddr_storage& addr);
};

// Free function: translate an ErrorContext into a human-readable string.
// Format: "<description> [<sysCode>: <system text>]"
// Both SocketImpl::getErrorMessage() and SocketException::what() call this;
// keeping the platform-specific translation in one place.
std::string formatErrorContext(const ErrorContext& ctx);

} // namespace aiSocks

#endif // AISOCKS_SOCKET_IMPL_H
