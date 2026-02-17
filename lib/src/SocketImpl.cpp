#include "SocketImpl.h"
#include <cstring>
#include <sstream>

namespace aiSocks {

// Platform-specific initialization
#ifdef _WIN32
static bool wsaInitialized = false;

bool SocketImpl::platformInit() {
    if (!wsaInitialized) {
        WSADATA wsaData;
        int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (result != 0) {
            return false;
        }
        wsaInitialized = true;
    }
    return true;
}

void SocketImpl::platformCleanup() {
    if (wsaInitialized) {
        WSACleanup();
        wsaInitialized = false;
    }
}
#else
bool SocketImpl::platformInit() {
    // Unix systems don't need initialization
    return true;
}

void SocketImpl::platformCleanup() {
    // Unix systems don't need cleanup
}
#endif

SocketImpl::SocketImpl(SocketType type, AddressFamily family)
    : socketHandle(INVALID_SOCKET_HANDLE)
    , socketType(type)
    , addressFamily(family)
    , lastError(SocketError::None)
    , blockingMode(true)
{
    platformInit();

    int af = (family == AddressFamily::IPv6) ? AF_INET6 : AF_INET;
    int sockType = (type == SocketType::TCP) ? SOCK_STREAM : SOCK_DGRAM;
    int protocol = (type == SocketType::TCP) ? IPPROTO_TCP : IPPROTO_UDP;

    socketHandle = socket(af, sockType, protocol);

    if (socketHandle == INVALID_SOCKET_HANDLE) {
        setError(SocketError::CreateFailed, "Failed to create socket");
    }
}

SocketImpl::SocketImpl(SocketHandle handle, SocketType type, AddressFamily family)
    : socketHandle(handle)
    , socketType(type)
    , addressFamily(family)
    , lastError(SocketError::None)
    , blockingMode(true)
{
}

SocketImpl::~SocketImpl() {
    close();
}

bool SocketImpl::bind(const std::string& address, uint16_t port) {
    if (!isValid()) {
        setError(SocketError::InvalidSocket, "Socket is not valid");
        return false;
    }

    if (addressFamily == AddressFamily::IPv6) {
        sockaddr_in6 addr{};
        addr.sin6_family = AF_INET6;
        addr.sin6_port = htons(port);

        if (address.empty() || address == "::" || address == "0.0.0.0") {
            addr.sin6_addr = in6addr_any;
        } else {
            if (inet_pton(AF_INET6, address.c_str(), &addr.sin6_addr) <= 0) {
                setError(SocketError::BindFailed, "Invalid IPv6 address");
                return false;
            }
        }

        if (::bind(socketHandle, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR_CODE) {
            setError(SocketError::BindFailed, "Failed to bind socket");
            return false;
        }
    } else {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);

        if (address.empty() || address == "0.0.0.0") {
            addr.sin_addr.s_addr = INADDR_ANY;
        } else {
            if (inet_pton(AF_INET, address.c_str(), &addr.sin_addr) <= 0) {
                setError(SocketError::BindFailed, "Invalid IPv4 address");
                return false;
            }
        }

        if (::bind(socketHandle, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR_CODE) {
            setError(SocketError::BindFailed, "Failed to bind socket");
            return false;
        }
    }

    lastError = SocketError::None;
    return true;
}

bool SocketImpl::listen(int backlog) {
    if (!isValid()) {
        setError(SocketError::InvalidSocket, "Socket is not valid");
        return false;
    }

    if (::listen(socketHandle, backlog) == SOCKET_ERROR_CODE) {
        setError(SocketError::ListenFailed, "Failed to listen on socket");
        return false;
    }

    lastError = SocketError::None;
    return true;
}

std::unique_ptr<SocketImpl> SocketImpl::accept() {
    if (!isValid()) {
        setError(SocketError::InvalidSocket, "Socket is not valid");
        return nullptr;
    }

    sockaddr_storage clientAddr{};
    socklen_t clientAddrLen = sizeof(clientAddr);

    SocketHandle clientSocket = ::accept(socketHandle, 
                                         reinterpret_cast<sockaddr*>(&clientAddr), 
                                         &clientAddrLen);

    if (clientSocket == INVALID_SOCKET_HANDLE) {
        setError(SocketError::AcceptFailed, "Failed to accept connection");
        return nullptr;
    }

    // Determine address family from accepted connection
    AddressFamily clientFamily = (reinterpret_cast<sockaddr*>(&clientAddr)->sa_family == AF_INET6) 
                                 ? AddressFamily::IPv6 : AddressFamily::IPv4;

    lastError = SocketError::None;
    return std::make_unique<SocketImpl>(clientSocket, socketType, clientFamily);
}

bool SocketImpl::connect(const std::string& address, uint16_t port) {
    if (!isValid()) {
        setError(SocketError::InvalidSocket, "Socket is not valid");
        return false;
    }

    if (addressFamily == AddressFamily::IPv6) {
        sockaddr_in6 serverAddr{};
        serverAddr.sin6_family = AF_INET6;
        serverAddr.sin6_port = htons(port);
        
        if (inet_pton(AF_INET6, address.c_str(), &serverAddr.sin6_addr) <= 0) {
            // Try to resolve hostname
            struct addrinfo hints{}, *result = nullptr;
            hints.ai_family = AF_INET6;
            hints.ai_socktype = (socketType == SocketType::TCP) ? SOCK_STREAM : SOCK_DGRAM;

            if (getaddrinfo(address.c_str(), nullptr, &hints, &result) != 0) {
                setError(SocketError::ConnectFailed, "Failed to resolve hostname");
                return false;
            }

            serverAddr = *reinterpret_cast<sockaddr_in6*>(result->ai_addr);
            serverAddr.sin6_port = htons(port);
            freeaddrinfo(result);
        }

        if (::connect(socketHandle, reinterpret_cast<sockaddr*>(&serverAddr), 
                      sizeof(serverAddr)) == SOCKET_ERROR_CODE) {
            setError(SocketError::ConnectFailed, "Failed to connect to server");
            return false;
        }
    } else {
        sockaddr_in serverAddr{};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(port);
        
        if (inet_pton(AF_INET, address.c_str(), &serverAddr.sin_addr) <= 0) {
            // Try to resolve hostname
            struct addrinfo hints{}, *result = nullptr;
            hints.ai_family = AF_INET;
            hints.ai_socktype = (socketType == SocketType::TCP) ? SOCK_STREAM : SOCK_DGRAM;

            if (getaddrinfo(address.c_str(), nullptr, &hints, &result) != 0) {
                setError(SocketError::ConnectFailed, "Failed to resolve hostname");
                return false;
            }

            serverAddr = *reinterpret_cast<sockaddr_in*>(result->ai_addr);
            serverAddr.sin_port = htons(port);
            freeaddrinfo(result);
        }

        if (::connect(socketHandle, reinterpret_cast<sockaddr*>(&serverAddr), 
                      sizeof(serverAddr)) == SOCKET_ERROR_CODE) {
            setError(SocketError::ConnectFailed, "Failed to connect to server");
            return false;
        }
    }

    lastError = SocketError::None;
    return true;
}

int SocketImpl::send(const void* data, size_t length) {
    if (!isValid()) {
        setError(SocketError::InvalidSocket, "Socket is not valid");
        return -1;
    }

#ifdef _WIN32
    int bytesSent = ::send(socketHandle, static_cast<const char*>(data), 
                           static_cast<int>(length), 0);
#else
    ssize_t bytesSent = ::send(socketHandle, data, length, 0);
#endif

    if (bytesSent == SOCKET_ERROR_CODE) {
        int error = getLastSystemError();
#ifdef _WIN32
        if (error == WSAEWOULDBLOCK) {
#else
        if (error == EWOULDBLOCK || error == EAGAIN) {
#endif
            setError(SocketError::WouldBlock, "Operation would block");
        } else {
            setError(SocketError::SendFailed, "Failed to send data");
        }
        return -1;
    }

    lastError = SocketError::None;
    return static_cast<int>(bytesSent);
}

int SocketImpl::receive(void* buffer, size_t length) {
    if (!isValid()) {
        setError(SocketError::InvalidSocket, "Socket is not valid");
        return -1;
    }

#ifdef _WIN32
    int bytesReceived = ::recv(socketHandle, static_cast<char*>(buffer), 
                               static_cast<int>(length), 0);
#else
    ssize_t bytesReceived = ::recv(socketHandle, buffer, length, 0);
#endif

    if (bytesReceived == SOCKET_ERROR_CODE) {
        int error = getLastSystemError();
#ifdef _WIN32
        if (error == WSAEWOULDBLOCK) {
#else
        if (error == EWOULDBLOCK || error == EAGAIN) {
#endif
            setError(SocketError::WouldBlock, "Operation would block");
        } else {
            setError(SocketError::ReceiveFailed, "Failed to receive data");
        }
        return -1;
    }

    lastError = SocketError::None;
    return static_cast<int>(bytesReceived);
}

bool SocketImpl::setBlocking(bool blocking) {
    if (!isValid()) {
        setError(SocketError::InvalidSocket, "Socket is not valid");
        return false;
    }

#ifdef _WIN32
    u_long mode = blocking ? 0 : 1;
    if (ioctlsocket(socketHandle, FIONBIO, &mode) != 0) {
        setError(SocketError::SetOptionFailed, "Failed to set blocking mode");
        return false;
    }
#else
    int flags = fcntl(socketHandle, F_GETFL, 0);
    if (flags == -1) {
        setError(SocketError::SetOptionFailed, "Failed to get socket flags");
        return false;
    }

    flags = blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
    if (fcntl(socketHandle, F_SETFL, flags) == -1) {
        setError(SocketError::SetOptionFailed, "Failed to set blocking mode");
        return false;
    }
#endif

    blockingMode = blocking;
    lastError = SocketError::None;
    return true;
}

bool SocketImpl::isBlocking() const {
    return blockingMode;
}

bool SocketImpl::setReuseAddress(bool reuse) {
    if (!isValid()) {
        setError(SocketError::InvalidSocket, "Socket is not valid");
        return false;
    }

    int optval = reuse ? 1 : 0;
    if (setsockopt(socketHandle, SOL_SOCKET, SO_REUSEADDR, 
                   reinterpret_cast<const char*>(&optval), sizeof(optval)) == SOCKET_ERROR_CODE) {
        setError(SocketError::SetOptionFailed, "Failed to set reuse address option");
        return false;
    }

    lastError = SocketError::None;
    return true;
}

bool SocketImpl::setTimeout(int seconds) {
    if (!isValid()) {
        setError(SocketError::InvalidSocket, "Socket is not valid");
        return false;
    }

#ifdef _WIN32
    DWORD timeout = seconds * 1000;
    if (setsockopt(socketHandle, SOL_SOCKET, SO_RCVTIMEO, 
                   reinterpret_cast<const char*>(&timeout), sizeof(timeout)) == SOCKET_ERROR_CODE) {
        setError(SocketError::SetOptionFailed, "Failed to set timeout");
        return false;
    }
#else
    struct timeval tv;
    tv.tv_sec = seconds;
    tv.tv_usec = 0;
    if (setsockopt(socketHandle, SOL_SOCKET, SO_RCVTIMEO, 
                   &tv, sizeof(tv)) == SOCKET_ERROR_CODE) {
        setError(SocketError::SetOptionFailed, "Failed to set timeout");
        return false;
    }
#endif

    lastError = SocketError::None;
    return true;
}

void SocketImpl::close() {
    if (isValid()) {
#ifdef _WIN32
        closesocket(socketHandle);
#else
        ::close(socketHandle);
#endif
        socketHandle = INVALID_SOCKET_HANDLE;
    }
}

bool SocketImpl::isValid() const {
    return socketHandle != INVALID_SOCKET_HANDLE;
}

AddressFamily SocketImpl::getAddressFamily() const {
    return addressFamily;
}

SocketError SocketImpl::getLastError() const {
    return lastError;
}

std::string SocketImpl::getErrorMessage() const {
    return lastErrorMessage;
}

void SocketImpl::setError(SocketError error, const std::string& message) {
    lastError = error;
    std::ostringstream oss;
    oss << message << " (System error: " << getLastSystemError() << ")";
    lastErrorMessage = oss.str();
}

int SocketImpl::getLastSystemError() const {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

} // namespace aiSocks
