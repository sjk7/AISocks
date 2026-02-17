// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
#include "SocketImpl.h"
#include <chrono>
#include <cstring>
#include <sstream>
#ifndef _WIN32
#include <signal.h>
#endif

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
    // Suppress SIGPIPE process-wide.  Belt-and-suspenders with SO_NOSIGPIPE
    // (macOS, set per-socket) and MSG_NOSIGNAL (Linux, set per-call): this
    // catches any remaining path that bypasses those per-socket/per-call
    // guards.
    ::signal(SIGPIPE, SIG_IGN);
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
    , blockingMode(true) {
    platformInit();

    int af = (family == AddressFamily::IPv6) ? AF_INET6 : AF_INET;
    int sockType = (type == SocketType::TCP) ? SOCK_STREAM : SOCK_DGRAM;
    int protocol = (type == SocketType::TCP) ? IPPROTO_TCP : IPPROTO_UDP;

    socketHandle = socket(af, sockType, protocol);

    if (socketHandle == INVALID_SOCKET_HANDLE) {
        setError(SocketError::CreateFailed, "Failed to create socket");
        return;
    }

#ifdef SO_NOSIGPIPE
    // macOS: prevent send/write to a half-closed socket from raising SIGPIPE.
    // Linux uses MSG_NOSIGNAL per-call instead; Windows has no SIGPIPE concept.
    int noSigPipe = 1;
    ::setsockopt(
        socketHandle, SOL_SOCKET, SO_NOSIGPIPE, &noSigPipe, sizeof(noSigPipe));
#endif
}

SocketImpl::SocketImpl(
    SocketHandle handle, SocketType type, AddressFamily family)
    : socketHandle(handle)
    , socketType(type)
    , addressFamily(family)
    , lastError(SocketError::None)
    , blockingMode(true) {
#ifdef SO_NOSIGPIPE
    if (socketHandle != INVALID_SOCKET_HANDLE) {
        int noSigPipe = 1;
        ::setsockopt(socketHandle, SOL_SOCKET, SO_NOSIGPIPE, &noSigPipe,
            sizeof(noSigPipe));
    }
#endif
}

SocketImpl::~SocketImpl() {
    if (isValid()) {
        // Gracefully shutdown the connection before closing
#ifdef _WIN32
        ::shutdown(socketHandle, SD_BOTH);
#else
        ::shutdown(socketHandle, SHUT_RDWR);
#endif
    }
    close();
}

bool SocketImpl::bind(const std::string& address, Port port) {
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

        if (::bind(
                socketHandle, reinterpret_cast<sockaddr*>(&addr), sizeof(addr))
            == SOCKET_ERROR_CODE) {
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

        if (::bind(
                socketHandle, reinterpret_cast<sockaddr*>(&addr), sizeof(addr))
            == SOCKET_ERROR_CODE) {
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

    SocketHandle clientSocket = ::accept(
        socketHandle, reinterpret_cast<sockaddr*>(&clientAddr), &clientAddrLen);

    if (clientSocket == INVALID_SOCKET_HANDLE) {
        setError(SocketError::AcceptFailed, "Failed to accept connection");
        return nullptr;
    }

    // Determine address family from accepted connection
    AddressFamily clientFamily
        = (reinterpret_cast<sockaddr*>(&clientAddr)->sa_family == AF_INET6)
        ? AddressFamily::IPv6
        : AddressFamily::IPv4;

    lastError = SocketError::None;
    return std::make_unique<SocketImpl>(clientSocket, socketType, clientFamily);
}

bool SocketImpl::connect(
    const std::string& address, Port port, std::chrono::milliseconds timeout) {
    if (!isValid()) {
        setError(SocketError::InvalidSocket, "Socket is not valid");
        return false;
    }

    // --- Phase 1: resolve address (synchronous; DNS is a known limitation
    //     of this single-threaded library --- no timeout applies here) -------
    sockaddr_storage serverAddr{};
    socklen_t addrLen = 0;

    if (addressFamily == AddressFamily::IPv6) {
        auto* a6 = reinterpret_cast<sockaddr_in6*>(&serverAddr);
        a6->sin6_family = AF_INET6;
        a6->sin6_port = htons(port);
        if (inet_pton(AF_INET6, address.c_str(), &a6->sin6_addr) <= 0) {
            struct addrinfo hints{}, *res = nullptr;
            hints.ai_family = AF_INET6;
            hints.ai_socktype
                = (socketType == SocketType::TCP) ? SOCK_STREAM : SOCK_DGRAM;
            int gaiErr6 = getaddrinfo(address.c_str(), nullptr, &hints, &res);
            if (gaiErr6 != 0) {
#ifdef _WIN32
                // getaddrinfo sets WSAGetLastError() on Windows
                setError(SocketError::ConnectFailed,
                    "Failed to resolve '" + address + "'");
#else
                setErrorDns(SocketError::ConnectFailed,
                    "Failed to resolve '" + address + "'", gaiErr6);
#endif
                return false;
            }
            *a6 = *reinterpret_cast<sockaddr_in6*>(res->ai_addr);
            a6->sin6_port = htons(port);
            freeaddrinfo(res);
        }
        addrLen = sizeof(sockaddr_in6);
    } else {
        auto* a4 = reinterpret_cast<sockaddr_in*>(&serverAddr);
        a4->sin_family = AF_INET;
        a4->sin_port = htons(port);
        if (inet_pton(AF_INET, address.c_str(), &a4->sin_addr) <= 0) {
            struct addrinfo hints{}, *res = nullptr;
            hints.ai_family = AF_INET;
            hints.ai_socktype
                = (socketType == SocketType::TCP) ? SOCK_STREAM : SOCK_DGRAM;
            int gaiErr4 = getaddrinfo(address.c_str(), nullptr, &hints, &res);
            if (gaiErr4 != 0) {
#ifdef _WIN32
                setError(SocketError::ConnectFailed,
                    "Failed to resolve '" + address + "'");
#else
                setErrorDns(SocketError::ConnectFailed,
                    "Failed to resolve '" + address + "'", gaiErr4);
#endif
                return false;
            }
            *a4 = *reinterpret_cast<sockaddr_in*>(res->ai_addr);
            a4->sin_port = htons(port);
            freeaddrinfo(res);
        }
        addrLen = sizeof(sockaddr_in);
    }

    // --- Phase 2: connect (with optional select()-based timeout) ------------
    if (timeout.count() <= 0) {
        // Blocking connect.
        if (::connect(
                socketHandle, reinterpret_cast<sockaddr*>(&serverAddr), addrLen)
            == SOCKET_ERROR_CODE) {
            setError(SocketError::ConnectFailed, "Failed to connect to server");
            return false;
        }
        lastError = SocketError::None;
        return true;
    }

    // Switch to non-blocking so connect() returns immediately.
#ifdef _WIN32
    u_long nbMode = 1;
    ioctlsocket(socketHandle, FIONBIO, &nbMode);
#else
    int savedFlags = fcntl(socketHandle, F_GETFL, 0);
    fcntl(socketHandle, F_SETFL, savedFlags | O_NONBLOCK);
#endif

    // Restore flags and sync blockingMode on any exit path.
    auto restoreBlocking = [&]() {
#ifdef _WIN32
        u_long blkMode = 0;
        ioctlsocket(socketHandle, FIONBIO, &blkMode);
        blockingMode = true;
#else
        fcntl(socketHandle, F_SETFL, savedFlags);
        blockingMode = (savedFlags & O_NONBLOCK) == 0;
#endif
    };

    int rc = ::connect(
        socketHandle, reinterpret_cast<sockaddr*>(&serverAddr), addrLen);
    if (rc == 0) {
        // Immediate success (rare but valid on loopback).
        restoreBlocking();
        lastError = SocketError::None;
        return true;
    }

    int sysErr = getLastSystemError();
    bool inProgress =
#ifdef _WIN32
        (sysErr == WSAEWOULDBLOCK) || (sysErr == WSAEINPROGRESS);
#else
        (sysErr == EINPROGRESS) || (sysErr == EAGAIN);
#endif

    if (!inProgress) {
        setError(SocketError::ConnectFailed, "Failed to connect to server");
        restoreBlocking();
        return false;
    }

    // Poll with short intervals so this thread blocks for at most
    // POLL_INTERVAL_MS at a time, keeping it responsive to cancellation.
    // The deadline is measured with steady_clock (monotonic) so it is
    // immune to wall-clock adjustments and EINTR restarts cost only the
    // remaining slice, not the full timeout.
    static constexpr int POLL_INTERVAL_MS = 10;
    auto deadline = std::chrono::steady_clock::now() + timeout;

    for (;;) {
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now())
                             .count();
        if (remaining <= 0) {
            setError(SocketError::Timeout,
                "connect() timed out after " + std::to_string(timeout.count())
                    + " ms");
            restoreBlocking();
            return false;
        }

        // Use the shorter of the poll interval and remaining time.
        long long sliceMs
            = (remaining < POLL_INTERVAL_MS) ? remaining : POLL_INTERVAL_MS;
        struct timeval tv;
        tv.tv_sec = static_cast<long>(sliceMs / 1000);
        tv.tv_usec = static_cast<long>((sliceMs % 1000) * 1000);

        fd_set writeSet, errSet;
        FD_ZERO(&writeSet);
        FD_SET(socketHandle, &writeSet);
        FD_ZERO(&errSet);
        FD_SET(socketHandle, &errSet);

        int sel = ::select(static_cast<int>(socketHandle) + 1, nullptr,
            &writeSet, &errSet, &tv);

        if (sel < 0) {
#ifndef _WIN32
            if (errno == EINTR) continue; // signal interrupted; retry
#endif
            setError(
                SocketError::ConnectFailed, "select() failed during connect");
            restoreBlocking();
            return false;
        }

        if (sel == 0) {
            // Slice timed out; check overall deadline at top of loop.
            continue;
        }

        // select() returned > 0: check whether the connection succeeded.
        int sockErr = 0;
        socklen_t sockErrLen = sizeof(sockErr);
        getsockopt(socketHandle, SOL_SOCKET, SO_ERROR,
            reinterpret_cast<char*>(&sockErr), &sockErrLen);
        if (sockErr != 0) {
#ifdef _WIN32
            WSASetLastError(sockErr);
#else
            errno = sockErr;
#endif
            setError(SocketError::ConnectFailed, "Failed to connect to server");
            restoreBlocking();
            return false;
        }

        restoreBlocking();
        lastError = SocketError::None;
        return true;
    }
}

int SocketImpl::send(const void* data, size_t length) {
    if (!isValid()) {
        setError(SocketError::InvalidSocket, "Socket is not valid");
        return -1;
    }

    for (;;) {
#ifdef _WIN32
        int bytesSent = ::send(socketHandle, static_cast<const char*>(data),
            static_cast<int>(length), 0);
#elif defined(MSG_NOSIGNAL)
        // Linux: return EPIPE instead of raising SIGPIPE on broken connection.
        ssize_t bytesSent = ::send(socketHandle, data, length, MSG_NOSIGNAL);
#else
        ssize_t bytesSent = ::send(socketHandle, data, length, 0);
#endif
        if (bytesSent != SOCKET_ERROR_CODE) {
            lastError = SocketError::None;
            return static_cast<int>(bytesSent);
        }

        int error = getLastSystemError();
#ifndef _WIN32
        if (error == EINTR) continue; // signal interrupted; retry transparently
#endif
#ifdef _WIN32
        if (error == WSAEWOULDBLOCK)
#else
        if (error == EWOULDBLOCK || error == EAGAIN)
#endif
        {
            setError(SocketError::WouldBlock, "Operation would block");
            return -1;
        }
#ifdef _WIN32
        if (error == WSAETIMEDOUT)
#else
        if (error == ETIMEDOUT)
#endif
        {
            setError(SocketError::Timeout, "send() timed out");
            return -1;
        }
        setError(SocketError::SendFailed, "Failed to send data");
        return -1;
    }
}

int SocketImpl::receive(void* buffer, size_t length) {
    if (!isValid()) {
        setError(SocketError::InvalidSocket, "Socket is not valid");
        return -1;
    }

    for (;;) {
#ifdef _WIN32
        int bytesReceived = ::recv(socketHandle, static_cast<char*>(buffer),
            static_cast<int>(length), 0);
#else
        ssize_t bytesReceived = ::recv(socketHandle, buffer, length, 0);
#endif
        if (bytesReceived != SOCKET_ERROR_CODE) {
            lastError = SocketError::None;
            return static_cast<int>(bytesReceived);
        }

        int error = getLastSystemError();
#ifndef _WIN32
        if (error == EINTR) continue; // signal interrupted; retry transparently
#endif
#ifdef _WIN32
        if (error == WSAEWOULDBLOCK)
#else
        if (error == EWOULDBLOCK || error == EAGAIN)
#endif
        {
            setError(SocketError::WouldBlock, "Operation would block");
            return -1;
        }
#ifdef _WIN32
        if (error == WSAETIMEDOUT)
#else
        if (error == ETIMEDOUT)
#endif
        {
            setError(SocketError::Timeout, "recv() timed out");
            return -1;
        }
        setError(SocketError::ReceiveFailed, "Failed to receive data");
        return -1;
    }
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
            reinterpret_cast<const char*>(&optval), sizeof(optval))
        == SOCKET_ERROR_CODE) {
        setError(
            SocketError::SetOptionFailed, "Failed to set reuse address option");
        return false;
    }

    lastError = SocketError::None;
    return true;
}

bool SocketImpl::setTimeout(std::chrono::milliseconds timeout) {
    if (!isValid()) {
        setError(SocketError::InvalidSocket, "Socket is not valid");
        return false;
    }

    const long long ms = timeout.count();

#ifdef _WIN32
    DWORD tv = static_cast<DWORD>(ms);
    if (setsockopt(socketHandle, SOL_SOCKET, SO_RCVTIMEO,
            reinterpret_cast<const char*>(&tv), sizeof(tv))
        == SOCKET_ERROR_CODE) {
        setError(SocketError::SetOptionFailed, "Failed to set timeout");
        return false;
    }
#else
    struct timeval tv;
    tv.tv_sec = static_cast<long>(ms / 1000);
    tv.tv_usec = static_cast<long>((ms % 1000) * 1000);
    if (setsockopt(socketHandle, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv))
        == SOCKET_ERROR_CODE) {
        setError(SocketError::SetOptionFailed, "Failed to set timeout");
        return false;
    }
#endif

    lastError = SocketError::None;
    return true;
}

void SocketImpl::close() {
    if (isValid()) {
        // Attempt graceful shutdown (ignore errors as socket may not be
        // connected)
#ifdef _WIN32
        ::shutdown(socketHandle, SD_BOTH);
        closesocket(socketHandle);
#else
        ::shutdown(socketHandle, SHUT_RDWR);
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

std::string formatErrorContext(const ErrorContext& ctx) {
    std::string sysText;
#ifdef _WIN32
    (void)ctx.isDns; // Windows: FormatMessage handles all codes (errno + EAI_*)
    char buf[512] = {};
    DWORD len = FormatMessageA(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
        static_cast<DWORD>(ctx.sysCode), 0, buf, sizeof(buf), nullptr);
    while (len > 0 && (buf[len - 1] == '\r' || buf[len - 1] == '\n'))
        buf[--len] = '\0';
    sysText = buf;
#else
    sysText = ctx.isDns ? ::gai_strerror(ctx.sysCode) : ::strerror(ctx.sysCode);
#endif
    std::ostringstream oss;
    oss << ctx.description << " [" << ctx.sysCode << ": " << sysText << "]";
    return oss.str();
}

std::string SocketImpl::getErrorMessage() const {
    if (lastError == SocketError::None) return {};
    if (!errorMessageDirty) return lastErrorMessage;
    lastErrorMessage
        = formatErrorContext({lastErrorDesc, lastSysCode, lastErrorIsDns});
    errorMessageDirty = false;
    return lastErrorMessage;
}

void SocketImpl::setError(SocketError error, const std::string& description) {
    lastError = error;
    lastErrorDesc = description;
    lastSysCode = getLastSystemError(); // capture before next syscall
    lastErrorIsDns = false;
    errorMessageDirty = true;
}

void SocketImpl::setErrorDns(
    SocketError error, const std::string& description, int gaiCode) {
    // EAI_* codes are not errno values; flag so getErrorMessage() uses
    // gai_strerror() instead of strerror() / FormatMessage.
    lastError = error;
    lastErrorDesc = description;
    lastSysCode = gaiCode;
    lastErrorIsDns = true;
    errorMessageDirty = true;
}

ErrorContext SocketImpl::getErrorContext() const {
    return {lastErrorDesc, lastSysCode, lastErrorIsDns};
}

int SocketImpl::getLastSystemError() const {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

// -----------------------------------------------------------------------
// sendTo / receiveFrom (UDP)
// -----------------------------------------------------------------------
int SocketImpl::sendTo(
    const void* data, size_t length, const Endpoint& remote) {
    if (!isValid()) {
        setError(SocketError::InvalidSocket, "Socket is not valid");
        return -1;
    }

    sockaddr_storage addr{};
    socklen_t addrLen = 0;

    if (remote.family == AddressFamily::IPv6) {
        auto* a6 = reinterpret_cast<sockaddr_in6*>(&addr);
        a6->sin6_family = AF_INET6;
        a6->sin6_port = htons(remote.port);
        inet_pton(AF_INET6, remote.address.c_str(), &a6->sin6_addr);
        addrLen = sizeof(sockaddr_in6);
    } else {
        auto* a4 = reinterpret_cast<sockaddr_in*>(&addr);
        a4->sin_family = AF_INET;
        a4->sin_port = htons(remote.port);
        inet_pton(AF_INET, remote.address.c_str(), &a4->sin_addr);
        addrLen = sizeof(sockaddr_in);
    }

    for (;;) {
#ifdef _WIN32
        int sent = ::sendto(socketHandle, static_cast<const char*>(data),
            static_cast<int>(length), 0, reinterpret_cast<sockaddr*>(&addr),
            addrLen);
#elif defined(MSG_NOSIGNAL)
        ssize_t sent = ::sendto(socketHandle, data, length, MSG_NOSIGNAL,
            reinterpret_cast<sockaddr*>(&addr), addrLen);
#else
        ssize_t sent = ::sendto(socketHandle, data, length, 0,
            reinterpret_cast<sockaddr*>(&addr), addrLen);
#endif
        if (sent != SOCKET_ERROR_CODE) {
            lastError = SocketError::None;
            return static_cast<int>(sent);
        }

        int error = getLastSystemError();
#ifndef _WIN32
        if (error == EINTR) continue; // signal interrupted; retry transparently
#endif
#ifdef _WIN32
        if (error == WSAEWOULDBLOCK)
#else
        if (error == EWOULDBLOCK || error == EAGAIN)
#endif
        {
            setError(SocketError::WouldBlock, "Operation would block");
            return -1;
        }
#ifdef _WIN32
        if (error == WSAETIMEDOUT)
#else
        if (error == ETIMEDOUT)
#endif
        {
            setError(SocketError::Timeout, "sendTo() timed out");
            return -1;
        }
        setError(SocketError::SendFailed, "sendTo() failed");
        return -1;
    }
}

int SocketImpl::receiveFrom(void* buffer, size_t length, Endpoint& remote) {
    if (!isValid()) {
        setError(SocketError::InvalidSocket, "Socket is not valid");
        return -1;
    }

    sockaddr_storage addr{};
    socklen_t addrLen = sizeof(addr);

    for (;;) {
#ifdef _WIN32
        int recvd = ::recvfrom(socketHandle, static_cast<char*>(buffer),
            static_cast<int>(length), 0, reinterpret_cast<sockaddr*>(&addr),
            &addrLen);
#else
        ssize_t recvd = ::recvfrom(socketHandle, buffer, length, 0,
            reinterpret_cast<sockaddr*>(&addr), &addrLen);
#endif
        if (recvd != SOCKET_ERROR_CODE) {
            remote = endpointFromSockaddr(addr);
            lastError = SocketError::None;
            return static_cast<int>(recvd);
        }

        int err = getLastSystemError();
#ifndef _WIN32
        if (err == EINTR) continue; // signal interrupted; retry transparently
#endif
#ifdef _WIN32
        if (err == WSAEWOULDBLOCK)
#else
        if (err == EWOULDBLOCK || err == EAGAIN)
#endif
        {
            setError(SocketError::WouldBlock, "Operation would block");
            return -1;
        }
#ifdef _WIN32
        if (err == WSAETIMEDOUT)
#else
        if (err == ETIMEDOUT)
#endif
        {
            setError(SocketError::Timeout, "recvfrom() timed out");
            return -1;
        }
        setError(SocketError::ReceiveFailed, "receiveFrom() failed");
        return -1;
    }
}

// -----------------------------------------------------------------------
// setSendTimeout
// -----------------------------------------------------------------------
bool SocketImpl::setSendTimeout(std::chrono::milliseconds timeout) {
    if (!isValid()) {
        setError(SocketError::InvalidSocket, "Socket is not valid");
        return false;
    }
    const long long ms = timeout.count();
#ifdef _WIN32
    DWORD tv = static_cast<DWORD>(ms);
    if (setsockopt(socketHandle, SOL_SOCKET, SO_SNDTIMEO,
            reinterpret_cast<const char*>(&tv), sizeof(tv))
        == SOCKET_ERROR_CODE) {
        setError(SocketError::SetOptionFailed, "Failed to set send timeout");
        return false;
    }
#else
    struct timeval tv;
    tv.tv_sec = static_cast<long>(ms / 1000);
    tv.tv_usec = static_cast<long>((ms % 1000) * 1000);
    if (setsockopt(socketHandle, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv))
        == SOCKET_ERROR_CODE) {
        setError(SocketError::SetOptionFailed, "Failed to set send timeout");
        return false;
    }
#endif
    lastError = SocketError::None;
    return true;
}

// -----------------------------------------------------------------------
// setNoDelay (TCP_NODELAY)
// -----------------------------------------------------------------------
bool SocketImpl::setNoDelay(bool noDelay) {
    if (!isValid()) {
        setError(SocketError::InvalidSocket, "Socket is not valid");
        return false;
    }
    int optval = noDelay ? 1 : 0;
    if (setsockopt(socketHandle, IPPROTO_TCP, TCP_NODELAY,
            reinterpret_cast<const char*>(&optval), sizeof(optval))
        == SOCKET_ERROR_CODE) {
        setError(SocketError::SetOptionFailed, "Failed to set TCP_NODELAY");
        return false;
    }
    lastError = SocketError::None;
    return true;
}

// -----------------------------------------------------------------------
// setKeepAlive (SO_KEEPALIVE)
// -----------------------------------------------------------------------
bool SocketImpl::setKeepAlive(bool enable) {
    if (!isValid()) {
        setError(SocketError::InvalidSocket, "Socket is not valid");
        return false;
    }
    int optval = enable ? 1 : 0;
    if (setsockopt(socketHandle, SOL_SOCKET, SO_KEEPALIVE,
            reinterpret_cast<const char*>(&optval), sizeof(optval))
        == SOCKET_ERROR_CODE) {
        setError(SocketError::SetOptionFailed, "Failed to set SO_KEEPALIVE");
        return false;
    }
    lastError = SocketError::None;
    return true;
}

// -----------------------------------------------------------------------
// setReceiveBufferSize / setSendBufferSize (SO_RCVBUF / SO_SNDBUF)
// -----------------------------------------------------------------------
bool SocketImpl::setReceiveBufferSize(int bytes) {
    if (!isValid()) {
        setError(SocketError::InvalidSocket, "Socket is not valid");
        return false;
    }
    if (setsockopt(socketHandle, SOL_SOCKET, SO_RCVBUF,
            reinterpret_cast<const char*>(&bytes), sizeof(bytes))
        == SOCKET_ERROR_CODE) {
        setError(SocketError::SetOptionFailed, "Failed to set SO_RCVBUF");
        return false;
    }
    lastError = SocketError::None;
    return true;
}

bool SocketImpl::setSendBufferSize(int bytes) {
    if (!isValid()) {
        setError(SocketError::InvalidSocket, "Socket is not valid");
        return false;
    }
    if (setsockopt(socketHandle, SOL_SOCKET, SO_SNDBUF,
            reinterpret_cast<const char*>(&bytes), sizeof(bytes))
        == SOCKET_ERROR_CODE) {
        setError(SocketError::SetOptionFailed, "Failed to set SO_SNDBUF");
        return false;
    }
    lastError = SocketError::None;
    return true;
}

// -----------------------------------------------------------------------
// shutdown(ShutdownHow)
// -----------------------------------------------------------------------
bool SocketImpl::shutdown(ShutdownHow how) {
    if (!isValid()) {
        setError(SocketError::InvalidSocket, "Socket is not valid");
        return false;
    }
#ifdef _WIN32
    int how_ = (how == ShutdownHow::Read) ? SD_RECEIVE
        : (how == ShutdownHow::Write)     ? SD_SEND
                                          : SD_BOTH;
#else
    int how_ = (how == ShutdownHow::Read) ? SHUT_RD
        : (how == ShutdownHow::Write)     ? SHUT_WR
                                          : SHUT_RDWR;
#endif
    if (::shutdown(socketHandle, how_) == SOCKET_ERROR_CODE) {
        setError(SocketError::Unknown, "shutdown() failed");
        return false;
    }
    lastError = SocketError::None;
    return true;
}

// -----------------------------------------------------------------------
// getLocalEndpoint / getPeerEndpoint
// -----------------------------------------------------------------------
Endpoint SocketImpl::endpointFromSockaddr(const sockaddr_storage& addr) {
    Endpoint ep;
    if (addr.ss_family == AF_INET6) {
        ep.family = AddressFamily::IPv6;
        const auto* a6 = reinterpret_cast<const sockaddr_in6*>(&addr);
        ep.port = Port{ntohs(a6->sin6_port)};
        char buf[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &a6->sin6_addr, buf, sizeof(buf));
        ep.address = buf;
    } else {
        ep.family = AddressFamily::IPv4;
        const auto* a4 = reinterpret_cast<const sockaddr_in*>(&addr);
        ep.port = Port{ntohs(a4->sin_port)};
        char buf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &a4->sin_addr, buf, sizeof(buf));
        ep.address = buf;
    }
    return ep;
}

std::optional<Endpoint> SocketImpl::getLocalEndpoint() const {
    if (!isValid()) return std::nullopt;
    sockaddr_storage addr{};
    socklen_t len = sizeof(addr);
    if (getsockname(socketHandle, reinterpret_cast<sockaddr*>(&addr), &len)
        != 0)
        return std::nullopt;
    return endpointFromSockaddr(addr);
}

std::optional<Endpoint> SocketImpl::getPeerEndpoint() const {
    if (!isValid()) return std::nullopt;
    sockaddr_storage addr{};
    socklen_t len = sizeof(addr);
    if (getpeername(socketHandle, reinterpret_cast<sockaddr*>(&addr), &len)
        != 0)
        return std::nullopt;
    return endpointFromSockaddr(addr);
}

// Static utility methods
std::vector<NetworkInterface> SocketImpl::getLocalAddresses() {
    std::vector<NetworkInterface> interfaces;
    platformInit();

#ifdef _WIN32
    // Windows implementation using GetAdaptersAddresses
    ULONG bufferSize = 15000;
    PIP_ADAPTER_ADDRESSES addresses = nullptr;
    ULONG result;

    do {
        addresses = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(malloc(bufferSize));
        if (!addresses) {
            break;
        }

        result = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX,
            nullptr, addresses, &bufferSize);

        if (result == ERROR_BUFFER_OVERFLOW) {
            free(addresses);
            addresses = nullptr;
        }
    } while (result == ERROR_BUFFER_OVERFLOW);

    if (result == NO_ERROR && addresses) {
        for (PIP_ADAPTER_ADDRESSES adapter = addresses; adapter != nullptr;
            adapter = adapter->Next) {
            for (PIP_ADAPTER_UNICAST_ADDRESS unicast
                = adapter->FirstUnicastAddress;
                unicast != nullptr; unicast = unicast->Next) {

                NetworkInterface iface;
                iface.name = std::string(adapter->AdapterName);

                // Convert address to string
                sockaddr* sa = unicast->Address.lpSockaddr;
                if (sa->sa_family == AF_INET) {
                    char buffer[INET_ADDRSTRLEN];
                    sockaddr_in* sin = reinterpret_cast<sockaddr_in*>(sa);
                    inet_ntop(AF_INET, &sin->sin_addr, buffer, INET_ADDRSTRLEN);
                    iface.address = buffer;
                    iface.family = AddressFamily::IPv4;
                } else if (sa->sa_family == AF_INET6) {
                    char buffer[INET6_ADDRSTRLEN];
                    sockaddr_in6* sin6 = reinterpret_cast<sockaddr_in6*>(sa);
                    inet_ntop(
                        AF_INET6, &sin6->sin6_addr, buffer, INET6_ADDRSTRLEN);
                    iface.address = buffer;
                    iface.family = AddressFamily::IPv6;
                } else {
                    continue;
                }

                iface.isLoopback
                    = (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK);
                interfaces.push_back(iface);
            }
        }
        free(addresses);
    }
#else
    // Unix/Linux implementation using getifaddrs
    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) == 0) {
        for (struct ifaddrs* ifa = ifaddr; ifa != nullptr;
            ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr) {
                continue;
            }

            NetworkInterface iface;
            iface.name = ifa->ifa_name;

            int family = ifa->ifa_addr->sa_family;
            if (family == AF_INET) {
                char buffer[INET_ADDRSTRLEN];
                sockaddr_in* sin
                    = reinterpret_cast<sockaddr_in*>(ifa->ifa_addr);
                inet_ntop(AF_INET, &sin->sin_addr, buffer, INET_ADDRSTRLEN);
                iface.address = buffer;
                iface.family = AddressFamily::IPv4;
                iface.isLoopback
                    = (iface.name == "lo" || iface.address == "127.0.0.1");
                interfaces.push_back(iface);
            } else if (family == AF_INET6) {
                char buffer[INET6_ADDRSTRLEN];
                sockaddr_in6* sin6
                    = reinterpret_cast<sockaddr_in6*>(ifa->ifa_addr);
                inet_ntop(AF_INET6, &sin6->sin6_addr, buffer, INET6_ADDRSTRLEN);
                iface.address = buffer;
                iface.family = AddressFamily::IPv6;
                iface.isLoopback
                    = (iface.name == "lo" || iface.address == "::1");
                interfaces.push_back(iface);
            }
        }
        freeifaddrs(ifaddr);
    }
#endif

    return interfaces;
}

bool SocketImpl::isValidIPv4(const std::string& address) {
    struct sockaddr_in sa;
    return inet_pton(AF_INET, address.c_str(), &(sa.sin_addr)) == 1;
}

bool SocketImpl::isValidIPv6(const std::string& address) {
    struct sockaddr_in6 sa;
    return inet_pton(AF_INET6, address.c_str(), &(sa.sin6_addr)) == 1;
}

std::string SocketImpl::ipToString(const void* addr, AddressFamily family) {
    if (family == AddressFamily::IPv4) {
        char buffer[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, addr, buffer, INET_ADDRSTRLEN)) {
            return std::string(buffer);
        }
    } else if (family == AddressFamily::IPv6) {
        char buffer[INET6_ADDRSTRLEN];
        if (inet_ntop(AF_INET6, addr, buffer, INET6_ADDRSTRLEN)) {
            return std::string(buffer);
        }
    }
    return "";
}

} // namespace aiSocks
