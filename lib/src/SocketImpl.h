#ifndef AISOCKS_SOCKET_IMPL_H
#define AISOCKS_SOCKET_IMPL_H

#include "Socket.h"

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
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
    using SocketHandle = int;
    #define INVALID_SOCKET_HANDLE -1
    #define SOCKET_ERROR_CODE -1
#endif

namespace aiSocks {

class SocketImpl {
public:
    SocketImpl(SocketType type, AddressFamily family);
    ~SocketImpl();

    // Platform initialization/cleanup
    static bool platformInit();
    static void platformCleanup();

    // Server operations
    bool bind(const std::string& address, uint16_t port);
    bool listen(int backlog);
    std::unique_ptr<SocketImpl> accept();

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

    // Constructor for accepted connections (public for make_unique)
    SocketImpl(SocketHandle handle, SocketType type, AddressFamily family);

private:
    SocketHandle socketHandle;
    SocketType socketType;
    AddressFamily addressFamily;
    SocketError lastError;
    std::string lastErrorMessage;
    bool blockingMode;

    void setError(SocketError error, const std::string& message);
    int getLastSystemError() const;
};

} // namespace aiSocks

#endif // AISOCKS_SOCKET_IMPL_H
