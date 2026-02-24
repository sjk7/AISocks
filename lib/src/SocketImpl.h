// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
#ifndef AISOCKS_SOCKET_IMPL_H
#define AISOCKS_SOCKET_IMPL_H

#include "SocketTypes.h"
#include "Result.h"
#include <chrono>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#endif
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
#include <net/if.h> // IFF_LOOPBACK
#include <netinet/tcp.h>
using SocketHandle = int;
#define INVALID_SOCKET_HANDLE -1
#define SOCKET_ERROR_CODE -1
#endif

namespace aiSocks {

// Internal: raw ingredients captured at the point of failure.
// The human-readable string is produced lazily via formatErrorContext().
struct ErrorContext {
    const char* description{nullptr}; // string literal or c_str()  never owned
    int sysCode{0}; // errno / WSAGetLastError / EAI_*
    bool isDns{false}; // true  translate with gai_strerror
};

class SocketImpl {
    public:
    SocketImpl(SocketType type, AddressFamily family);
    SocketImpl(); // Creates an invalid socket (INVALID_SOCKET_HANDLE)
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
        Milliseconds timeout = defaultConnectTimeout);

    // Data transfer
    int send(const void* data, size_t length);
    int receive(void* buffer, size_t length);
    int sendTo(const void* data, size_t length, const Endpoint& remote);
    int receiveFrom(void* buffer, size_t length, Endpoint& remote);

    // Socket options
    bool setBlocking(bool blocking);
    bool isBlocking() const noexcept;
    bool waitReadable(Milliseconds timeout);
    bool waitWritable(Milliseconds timeout);
    bool setReuseAddress(bool reuse);
    bool setReusePort(bool enable);
    bool setTimeout(Milliseconds timeout); // used by setReceiveTimeout
    bool setReceiveTimeout(Milliseconds timeout);
    bool setSendTimeout(Milliseconds timeout);
    bool setKeepAlive(bool enable);
    bool setLingerAbort(bool enable);
    bool setNoDelay(bool noDelay);
    bool setReceiveBufferSize(int bytes);
    bool setSendBufferSize(int bytes);
    bool setBroadcast(bool enable);
    bool setMulticastTTL(int ttl);
    bool shutdown(ShutdownHow how);
    bool sendAll(const void* data, size_t length);
    bool receiveAll(void* buffer, size_t length);
    bool waitReadable(std::chrono::milliseconds timeout);
    bool waitWritable(std::chrono::milliseconds timeout);

    // Utility
    void close() noexcept;
    bool isValid() const noexcept;
    AddressFamily getAddressFamily() const noexcept;
    SocketError getLastError() const noexcept;
    std::string getErrorMessage() const;
    ErrorContext getErrorContext() const;
    Result<Endpoint> getLocalEndpoint() const;
    Result<Endpoint> getPeerEndpoint() const;
    SocketHandle getRawHandle() const noexcept { return socketHandle; }

    // Query socket options
    int getReceiveBufferSize() const;
    int getSendBufferSize() const;
    bool getNoDelay() const;

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
    // (strerror / gai_strerror / FormatMessage) is a pure intstring lookup
    // that is stable over time, so it is deferred to getErrorMessage().
    //
    // Hot-path errors (WouldBlock, etc.) store a pointer to a string literal
    // so setError() does zero allocation.  Cold-path errors (DNS, dynamic
    // messages) use lastErrorDynamic.  Exactly one is active at any time.
    const char* lastErrorLiteral{
        nullptr}; // points to static storage; not owned
    std::string lastErrorDynamic; // for runtime-constructed messages
    int lastSysCode{0}; // errno / WSAGetLastError / EAI_*
    bool lastErrorIsDns{false}; // true -> translate with gai_strerror

    // Lazy cache: built on first call to getErrorMessage() after each error.
    mutable std::string lastErrorMessage;
    mutable bool errorMessageDirty{false};

    bool blockingMode{true};
    // true after user calls shutdown(); close() skips redundant ::shutdown()
    bool shutdownCalled_{false};

    // Hot-path setter: stores a pointer to a string literal  zero allocation.
    void setError(SocketError error, const char* description) noexcept;

    // Cold-path setter: accepts a runtime-constructed std::string (e.g. DNS
    // errors where the address is embedded).  Moves into lastErrorDynamic.
    void setError(SocketError error, std::string description);

    // DNS variant: stores a getaddrinfo EAI_* code; getErrorMessage() will
    // translate it with gai_strerror (POSIX) rather than
    // strerror/FormatMessage.
    void setErrorDns(
        SocketError error, const std::string& description, int gaiCode);

    int getLastSystemError() const;

    // Private helpers that consolidate repeated setsockopt / select patterns.
    bool setBoolOpt(int level, int optname, bool val, const char* errMsg);
    bool setTimeoutOpt(
        int optname, std::chrono::milliseconds ms, const char* errMsg);
    bool setBufSizeOpt(int optname, int bytes, const char* errMsg);
    bool waitReady(bool forRead, std::chrono::milliseconds timeout);
    static Endpoint endpointFromSockaddr(const sockaddr_storage& addr);
};

// Free function: translate an ErrorContext into a human-readable string.
// Format: "<description> [<sysCode>: <system text>]"
// Both SocketImpl::getErrorMessage() and SocketException::what() call this;
// keeping the platform-specific translation in one place.
std::string formatErrorContext(const ErrorContext& ctx);

} // namespace aiSocks

#endif // AISOCKS_SOCKET_IMPL_H
