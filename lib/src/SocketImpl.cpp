// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
#ifdef _WIN32
#include "pch.h"
#endif
#include "SocketImpl.h"
#include "SocketImplHelpers.h"
#include <chrono>
#include <cstring>
#include <mutex>
#ifndef _WIN32
#include <signal.h>
#endif
// Platform-native poll headers used by the timed connect() loop.
#if defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/event.h>
#elif defined(__linux__)
#include <sys/epoll.h>
#elif defined(_WIN32)
// WSAPoll is declared in <winsock2.h>, already pulled in via SocketImpl.h.
#endif

namespace aiSocks {

// Platform-specific initialization
#ifdef _WIN32
static std::once_flag sWsaInitFlag;
static bool sWsaInitOk = false;

bool SocketImpl::platformInit() {
    std::call_once(sWsaInitFlag, []() {
        WSADATA wsaData;
        sWsaInitOk = (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0);
    });
    return sWsaInitOk;
}

void SocketImpl::platformCleanup() {
    if (sWsaInitOk) {
        WSACleanup();
    }
}
#else
static std::once_flag sPlatformInitFlag;

bool SocketImpl::platformInit() {
    // Suppress SIGPIPE process-wide.  Belt-and-suspenders with SO_NOSIGPIPE
    // (macOS, set per-socket) and MSG_NOSIGNAL (Linux, set per-call): this
    // catches any remaining path that bypasses those per-socket/per-call
    // guards.
    std::call_once(sPlatformInitFlag, []() { ::signal(SIGPIPE, SIG_IGN); });
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
    ::setsockopt(socketHandle, SOL_SOCKET, SO_NOSIGPIPE, &noSigPipe,
        static_cast<socklen_t>(sizeof(noSigPipe)));
#endif
}

SocketImpl::SocketImpl(
    SocketHandle handle, SocketType type, AddressFamily family)
    : socketHandle(handle)
    , socketType(type)
    , addressFamily(family)
    , lastError(SocketError::None)
    , blockingMode(true) {
    // Accepted sockets inherit the server fd's blocking mode, which may differ
    // from our default.  Query the kernel so isBlocking() reflects reality.
#ifndef _WIN32
    if (handle != INVALID_SOCKET_HANDLE) {
        int flags = ::fcntl(handle, F_GETFL, 0);
        if (flags != -1) blockingMode = (flags & O_NONBLOCK) == 0;
    }
#else
    // Windows: no portable way to query FIONBIO state; default (true) is
    // correct because accepted sockets always start in blocking mode on WinSock.
#endif
#ifdef SO_NOSIGPIPE
    if (socketHandle != INVALID_SOCKET_HANDLE) {
        int noSigPipe = 1;
        ::setsockopt(socketHandle, SOL_SOCKET, SO_NOSIGPIPE, &noSigPipe,
            static_cast<socklen_t>(sizeof(noSigPipe)));
    }
#endif
}

SocketImpl::~SocketImpl() {
    // close() handles graceful shutdown + fd release.
    close();
}

bool SocketImpl::bind(const std::string& address, Port port) {
    if (!isValid()) {
        setError(SocketError::InvalidSocket, "Socket is not valid");
        return false;
    }

    sockaddr_storage addr{};
    socklen_t addrLen = 0;
    auto res = resolveToSockaddr(address, port, addressFamily, socketType,
        /*doDns=*/false, addr, addrLen);
    if (res != SocketError::None) {
        setError(SocketError::BindFailed,
            addressFamily == AddressFamily::IPv6 ? "Invalid IPv6 address"
                                                 : "Invalid IPv4 address");
        return false;
    }

    if (::bind(socketHandle, reinterpret_cast<sockaddr*>(&addr), addrLen)
        == SOCKET_ERROR_CODE) {
        setError(SocketError::BindFailed, "Failed to bind socket");
        return false;
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
    socklen_t clientAddrLen = static_cast<socklen_t>(sizeof(clientAddr));

    for (;;) {
        SocketHandle clientSocket = ::accept(socketHandle,
            reinterpret_cast<sockaddr*>(&clientAddr), &clientAddrLen);

        if (clientSocket != INVALID_SOCKET_HANDLE) {
            AddressFamily clientFamily
                = (reinterpret_cast<sockaddr*>(&clientAddr)->sa_family
                      == AF_INET6)
                ? AddressFamily::IPv6
                : AddressFamily::IPv4;
            lastError = SocketError::None;
            return std::make_unique<SocketImpl>(
                clientSocket, socketType, clientFamily);
        }

        int err = getLastSystemError();
#ifndef _WIN32
        if (err == EINTR) continue; // signal interrupted; retry
#endif
#ifdef _WIN32
        if (err == WSAEWOULDBLOCK)
#else
        if (err == EWOULDBLOCK || err == EAGAIN)
#endif
        {
            setError(SocketError::WouldBlock, "No connection pending");
            return nullptr;
        }
        setError(SocketError::AcceptFailed, "Failed to accept connection");
        return nullptr;
    }
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
    {
        int gaiErr = 0;
        auto r = resolveToSockaddr(address, port, addressFamily, socketType,
            /*doDns=*/true, serverAddr, addrLen, &gaiErr);
        if (r != SocketError::None) {
#ifdef _WIN32
            setError(SocketError::ConnectFailed,
                "Failed to resolve '" + address + " port:" + std::to_string(port)+"'");
#else
            setErrorDns(SocketError::ConnectFailed,
                "Failed to resolve '" + address + " port:" + std::to_string(port) + "'", gaiErr);
#endif
            return false;
        }
    }

    // --- Phase 2: connect -------------------------------------------------------
    // Always use the non-blocking + event-queue path regardless of the current
    // socket mode.  A RAII guard captures the original blocking state and
    // restores it on every exit (success, timeout, error, and exception).
    // This means:
    //    A blocking socket comes back blocking after a successful connect.
    //    A non-blocking socket (caller set it before constructing) comes back
    //     non-blocking  the guard is a no-op in that case.
    //    timeout > 0   wait up to that long for the handshake.
    //    timeout <= 0  initiate and return WouldBlock immediately so the
    //     caller can drive completion via a Poller.

    // RAII: saves and restores the OS-level blocking flag on all exit paths.
    // Calls the platform API directly rather than setBlocking() to avoid
    // clobbering lastError  setBlocking() sets lastError=None on success,
    // which would erase whatever error connect() stored before returning false.
    struct BlockingGuard {
        SocketImpl& impl_;
#ifdef _WIN32
        bool wasBlocking_;
        explicit BlockingGuard(SocketImpl& impl)
            : impl_(impl), wasBlocking_(impl.isBlocking()) {
            if (wasBlocking_) {
                u_long nb = 1;
                ioctlsocket(impl_.socketHandle, FIONBIO, &nb);
                impl_.blockingMode = false;
            }
        }
        ~BlockingGuard() {
            if (wasBlocking_) {
                u_long blk = 0;
                ioctlsocket(impl_.socketHandle, FIONBIO, &blk);
                impl_.blockingMode = true;
            }
        }
#else
        int savedFlags_;
        explicit BlockingGuard(SocketImpl& impl)
            : impl_(impl), savedFlags_(fcntl(impl.socketHandle, F_GETFL, 0)) {
            // Set O_NONBLOCK if not already set.
            if ((savedFlags_ & O_NONBLOCK) == 0)
                fcntl(impl_.socketHandle, F_SETFL, savedFlags_ | O_NONBLOCK);
            impl_.blockingMode = false;
        }
        ~BlockingGuard() {
            fcntl(impl_.socketHandle, F_SETFL, savedFlags_);
            impl_.blockingMode = (savedFlags_ & O_NONBLOCK) == 0;
        }
#endif
    } blockingGuard(*this);

    int rc = ::connect(
        socketHandle, reinterpret_cast<sockaddr*>(&serverAddr), addrLen);
    if (rc == 0) {
        // Immediate success (common on loopback).
        lastError = SocketError::None;
        return true; // guard restores blocking mode
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
        return false; // guard restores blocking mode
    }

    // timeout <= 0: caller wants non-blocking initiation  return WouldBlock.
    // The guard restores the original blocking mode (which was already
    // non-blocking if the caller set it, so this is a no-op in that case).
    if (timeout.count() <= 0) {
        setError(SocketError::WouldBlock,
            "connect() in progress (non-blocking socket)");
        return false;
    }

    // Wait for the handshake using the platform-native event queue  the same
    // backend as the Poller class.  No FD_SETSIZE limit; no select().
    //
    // kqueue / epoll need a queue fd; WSAPoll is per-call.
    // EvFdGuard closes the queue fd on all exit paths.
    int evFd = -1;
    struct EvFdGuard {
        int& fd_;
        ~EvFdGuard() {
#if !defined(_WIN32)
            if (fd_ != -1) {
                ::close(fd_);
                fd_ = -1;
            }
#endif
        }
    } evFdGuard{evFd};

#if defined(__APPLE__) || defined(__FreeBSD__)
    evFd = ::kqueue();
    if (evFd == -1) {
        setError(SocketError::ConnectFailed, "kqueue() failed during connect");
        return false;
    }
    {
        struct kevent reg{};
        EV_SET(&reg, static_cast<uintptr_t>(socketHandle), EVFILT_WRITE,
            EV_ADD | EV_ENABLE, 0, 0, nullptr);
        if (::kevent(evFd, &reg, 1, nullptr, 0, nullptr) == -1) {
            setError(SocketError::ConnectFailed,
                "kevent() registration failed during connect");
            return false;
        }
    }
#elif defined(__linux__)
    evFd = ::epoll_create1(EPOLL_CLOEXEC);
    if (evFd == -1) {
        setError(SocketError::ConnectFailed,
            "epoll_create1() failed during connect");
        return false;
    }
    {
        struct epoll_event epev{};
        epev.events = EPOLLOUT | EPOLLERR;
        epev.data.fd = socketHandle;
        if (::epoll_ctl(evFd, EPOLL_CTL_ADD, socketHandle, &epev) == -1) {
            setError(SocketError::ConnectFailed,
                "epoll_ctl() failed during connect");
            return false;
        }
    }
#endif

    // Deadline loop: each iteration waits at most 100 ms so the monotonic
    // clock check stays responsive; EINTR restarts with the remaining slice.
    auto deadline = std::chrono::steady_clock::now() + timeout;

    for (;;) {
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now())
                             .count();
        if (remaining <= 0) {
            setError(SocketError::Timeout,
                "connect() timed out after " + std::to_string(timeout.count())
                    + " ms");
            return false;
        }
        const long long sliceMs = (remaining < 100) ? remaining : 100;

        int nReady = 0;
#if defined(_WIN32)
        WSAPOLLFD pfd{};
        pfd.fd = socketHandle;
        pfd.events = POLLWRNORM;
        nReady = ::WSAPoll(&pfd, 1, static_cast<int>(sliceMs));
        if (nReady < 0) {
            setError(
                SocketError::ConnectFailed, "WSAPoll() failed during connect");
            return false;
        }
#elif defined(__APPLE__) || defined(__FreeBSD__)
        struct kevent out{};
        struct timespec ts{};
        ts.tv_sec = static_cast<time_t>(sliceMs / 1000);
        ts.tv_nsec = static_cast<long>((sliceMs % 1000) * 1'000'000L);
        nReady = ::kevent(evFd, nullptr, 0, &out, 1, &ts);
        if (nReady < 0) {
            if (errno == EINTR) continue;
            setError(
                SocketError::ConnectFailed, "kevent() failed during connect");
            return false;
        }
#elif defined(__linux__)
        struct epoll_event outev{};
        nReady = ::epoll_wait(evFd, &outev, 1, static_cast<int>(sliceMs));
        if (nReady < 0) {
            if (errno == EINTR) continue;
            setError(SocketError::ConnectFailed,
                "epoll_wait() failed during connect");
            return false;
        }
#endif

        if (nReady == 0) continue; // slice elapsed; recheck deadline

        // An event fired  confirm success via SO_ERROR.
        int sockErr = 0;
        socklen_t sockErrLen = static_cast<socklen_t>(sizeof(sockErr));
        getsockopt(socketHandle, SOL_SOCKET, SO_ERROR,
            reinterpret_cast<char*>(&sockErr), &sockErrLen);
        if (sockErr != 0) {
#ifdef _WIN32
            WSASetLastError(sockErr);
#else
            errno = sockErr;
#endif
            setError(SocketError::ConnectFailed, "Failed to connect to server");
            return false;
        }

        lastError = SocketError::None;
        return true; // guard restores blocking mode
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

        int sysErr = getLastSystemError();
#ifndef _WIN32
        if (sysErr == EINTR)
            continue; // signal interrupted; retry transparently
#endif
        switch (classifyTransferSysError(sysErr)) {
            case SocketError::WouldBlock:
                setError(SocketError::WouldBlock, "Operation would block");
                return -1;
            case SocketError::Timeout:
                setError(SocketError::Timeout, "send() timed out");
                return -1;
            case SocketError::ConnectionReset:
                setError(
                    SocketError::ConnectionReset, "Connection reset by peer");
                return -1;
            default:
                setError(SocketError::SendFailed, "Failed to send data");
                return -1;
        }
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

        int sysErr = getLastSystemError();
#ifndef _WIN32
        if (sysErr == EINTR)
            continue; // signal interrupted; retry transparently
#endif
        switch (classifyTransferSysError(sysErr)) {
            case SocketError::WouldBlock:
                setError(SocketError::WouldBlock, "Operation would block");
                return -1;
            case SocketError::Timeout:
                setError(SocketError::Timeout, "recv() timed out");
                return -1;
            case SocketError::ConnectionReset:
                setError(
                    SocketError::ConnectionReset, "Connection reset by peer");
                return -1;
            default:
                setError(SocketError::ReceiveFailed, "Failed to receive data");
                return -1;
        }
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

bool SocketImpl::setReuseAddress(bool reuse) {
    RETURN_IF_INVALID();
    int optval = reuse ? 1 : 0;
    return setSocketOption(socketHandle, SOL_SOCKET, SO_REUSEADDR, optval, "Failed to set reuse address option");
}

bool SocketImpl::setTimeout(std::chrono::milliseconds timeout) {
    RETURN_IF_INVALID();
    return setSocketOptionTimeout(socketHandle, SO_RCVTIMEO, timeout, "Failed to set receive timeout");
}

void SocketImpl::close() noexcept {
    if (isValid()) {
        // Only call ::shutdown() if the user hasn't already done so.
        // If shutdown(Write) was called for a deliberate half-close sequence,
        // a second shutdown(RDWR) here could interfere with the FIN exchange
        // or send an RST with SO_LINGER.
        if (!shutdownCalled_) {
#ifdef _WIN32
            ::shutdown(socketHandle, SD_BOTH);
#else
            ::shutdown(socketHandle, SHUT_RDWR);
#endif
        }
#ifdef _WIN32
        closesocket(socketHandle);
#else
        ::close(socketHandle);
#endif
        socketHandle = INVALID_SOCKET_HANDLE;
        shutdownCalled_ = false;
    }
}

bool SocketImpl::isValid() const noexcept {
    return socketHandle != INVALID_SOCKET_HANDLE;
}

AddressFamily SocketImpl::getAddressFamily() const noexcept {
    return addressFamily;
}

SocketError SocketImpl::getLastError() const noexcept {
    return lastError;
}

std::string SocketImpl::getErrorMessage() const {
    if (lastError == SocketError::None) return {};
    if (!errorMessageDirty) return lastErrorMessage;
    const char* desc
        = lastErrorLiteral ? lastErrorLiteral : lastErrorDynamic.c_str();
    lastErrorMessage = formatErrorContext({desc, lastSysCode, lastErrorIsDns});
    errorMessageDirty = false;
    return lastErrorMessage;
}

// Hot-path: pointer store only  no heap allocation.
void SocketImpl::setError(SocketError error, const char* description) noexcept {
    lastError = error;
    lastErrorLiteral = description;
    lastErrorDynamic.clear();
    lastSysCode = getLastSystemError(); // capture before next syscall
    lastErrorIsDns = false;
    errorMessageDirty = true;
}

// Cold-path: runtime-constructed message (e.g. DNS errors with address
// embedded).
void SocketImpl::setError(SocketError error, std::string description) {
    lastError = error;
    lastErrorLiteral = nullptr;
    lastErrorDynamic = std::move(description);
    lastSysCode = getLastSystemError();
    lastErrorIsDns = false;
    errorMessageDirty = true;
}

void SocketImpl::setErrorDns(
    SocketError error, const std::string& description, int gaiCode) {
    // EAI_* codes are not errno values; flag so getErrorMessage() uses
    // gai_strerror() instead of strerror() / FormatMessage.
    lastError = error;
    lastErrorLiteral = nullptr;
    lastErrorDynamic = description;
    lastSysCode = gaiCode;
    lastErrorIsDns = true;
    errorMessageDirty = true;
}

ErrorContext SocketImpl::getErrorContext() const {
    return {lastErrorLiteral ? lastErrorLiteral : lastErrorDynamic.c_str(),
        lastSysCode, lastErrorIsDns};
}

int SocketImpl::getLastSystemError() const {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

int SocketImpl::sendTo(
    const void* data, size_t length, const Endpoint& remote) {
    if (!isValid()) {
        setError(SocketError::InvalidSocket, "Socket is not valid");
        return -1;
    }

    sockaddr_storage addr{};
    socklen_t addrLen = 0;
    {
        auto rr = resolveToSockaddr(remote.address, remote.port, remote.family,
            socketType, /*doDns=*/false, addr, addrLen);
        if (rr != SocketError::None) {
            setError(SocketError::SendFailed,
                "sendTo(): invalid destination address '" + remote.address
                    + "'");
            return -1;
        }
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

        int sysErr = getLastSystemError();
#ifndef _WIN32
        if (sysErr == EINTR)
            continue; // signal interrupted; retry transparently
#endif
        switch (classifyTransferSysError(sysErr)) {
            case SocketError::WouldBlock:
                setError(SocketError::WouldBlock, "Operation would block");
                return -1;
            case SocketError::Timeout:
                setError(SocketError::Timeout, "sendTo() timed out");
                return -1;
            case SocketError::ConnectionReset:
                setError(
                    SocketError::ConnectionReset, "Connection reset by peer");
                return -1;
            default:
                setError(SocketError::SendFailed, "sendTo() failed");
                return -1;
        }
    }
}

int SocketImpl::receiveFrom(void* buffer, size_t length, Endpoint& remote) {
    if (!isValid()) {
        setError(SocketError::InvalidSocket, "Socket is not valid");
        return -1;
    }

    sockaddr_storage addr{};
    socklen_t addrLen = static_cast<socklen_t>(sizeof(addr));

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

        int sysErr = getLastSystemError();
#ifndef _WIN32
        if (sysErr == EINTR)
            continue; // signal interrupted; retry transparently
#endif
        switch (classifyTransferSysError(sysErr)) {
            case SocketError::WouldBlock:
                setError(SocketError::WouldBlock, "Operation would block");
                return -1;
            case SocketError::Timeout:
                setError(SocketError::Timeout, "recvfrom() timed out");
                return -1;
            case SocketError::ConnectionReset:
                setError(
                    SocketError::ConnectionReset, "Connection reset by peer");
                return -1;
            default:
                setError(SocketError::ReceiveFailed, "receiveFrom() failed");
                return -1;
        }
    }
}

bool SocketImpl::setSendTimeout(std::chrono::milliseconds timeout) {
    RETURN_IF_INVALID();
    return setSocketOptionTimeout(socketHandle, SO_SNDTIMEO, timeout, "Failed to set send timeout");
}

bool SocketImpl::setNoDelay(bool noDelay) {
    RETURN_IF_INVALID();
    int optval = noDelay ? 1 : 0;
    return setSocketOption(socketHandle, IPPROTO_TCP, TCP_NODELAY, optval, "Failed to set TCP_NODELAY");
}

bool SocketImpl::setKeepAlive(bool enable) {
    RETURN_IF_INVALID();
    int optval = enable ? 1 : 0;
    return setSocketOption(socketHandle, SOL_SOCKET, SO_KEEPALIVE, optval, "Failed to set SO_KEEPALIVE");
}

bool SocketImpl::setLingerAbort(bool enable) {
    RETURN_IF_INVALID();
    struct linger lg{};
    lg.l_onoff = enable ? 1 : 0;
    lg.l_linger = 0; // l_linger=0  RST on close
    if (setsockopt(socketHandle, SOL_SOCKET, SO_LINGER,
            reinterpret_cast<const char*>(&lg),
            static_cast<socklen_t>(sizeof(lg)))
        == SOCKET_ERROR_CODE) {
        setError(SocketError::SetOptionFailed, "Failed to set SO_LINGER");
        return false;
    }
    SET_SUCCESS();
    return true;
}

bool SocketImpl::setReusePort(bool enable) {
    RETURN_IF_INVALID();
#ifdef SO_REUSEPORT
    int optval = enable ? 1 : 0;
    return setSocketOption(socketHandle, SOL_SOCKET, SO_REUSEPORT, optval, "Failed to set SO_REUSEPORT");
#else
    (void)enable;
    setError(SocketError::SetOptionFailed,
        "SO_REUSEPORT is not supported on this platform");
    return false;
#endif
}

bool SocketImpl::setBroadcast(bool enable) {
    RETURN_IF_INVALID();
    int optval = enable ? 1 : 0;
    return setSocketOption(socketHandle, SOL_SOCKET, SO_BROADCAST, optval, "Failed to set SO_BROADCAST");
}

bool SocketImpl::setMulticastTTL(int ttl) {
    RETURN_IF_INVALID();
    if (addressFamily == AddressFamily::IPv6) {
        return setSocketOption(socketHandle, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, ttl, "Failed to set IPV6_MULTICAST_HOPS");
    } else {
        return setSocketOption(socketHandle, IPPROTO_IP, IP_MULTICAST_TTL, ttl, "Failed to set IP_MULTICAST_TTL");
    }
}

bool SocketImpl::sendAll(const void* data, size_t length) {
    const auto* ptr = static_cast<const char*>(data);
    size_t remaining = length;
    while (remaining > 0) {
        int sent = send(ptr, remaining);
        if (sent < 0) {
            return false; // error already recorded by send()
        }
        ptr += static_cast<size_t>(sent);
        remaining -= static_cast<size_t>(sent);
    }
    lastError = SocketError::None;
    return true;
}

bool SocketImpl::receiveAll(void* buffer, size_t length) {
    auto* ptr = static_cast<char*>(buffer);
    size_t remaining = length;
    while (remaining > 0) {
        int got = receive(ptr, remaining);
        if (got < 0) {
            return false; // error already recorded by receive()
        }
        if (got == 0) {
            // Clean EOF before we got all the bytes  treat as an error.
            setError(SocketError::ConnectionReset,
                "Connection closed before all bytes received");
            return false;
        }
        ptr += static_cast<size_t>(got);
        remaining -= static_cast<size_t>(got);
    }
    lastError = SocketError::None;
    return true;
}

bool SocketImpl::waitReadable(std::chrono::milliseconds timeout) {
    RETURN_IF_INVALID();
    // Use the same OS-specific polling logic as connect()
    auto deadline = std::chrono::steady_clock::now() + timeout;

    for (;;) {
        auto sliceMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        if (sliceMs < 0) return false; // Timeout

#ifdef __APPLE__
        // Use kqueue for waitReadable
        int evFd = ::kqueue();
        if (evFd == -1) return false;
        
        struct kevent reg{};
        EV_SET(&reg, static_cast<uintptr_t>(socketHandle), EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);
        if (::kevent(evFd, &reg, 1, nullptr, 0, nullptr) == -1) {
            ::close(evFd);
            return false;
        }
        
        struct timespec ts{};
        ts.tv_sec = sliceMs / 1000;
        ts.tv_nsec = (sliceMs % 1000) * 1000000;
        
        struct kevent out{};
        int nReady = ::kevent(evFd, nullptr, 0, &out, 1, &ts);
        ::close(evFd);
        
        if (nReady < 0) return false;
        if (nReady == 0) {
            setError(SocketError::Timeout, "waitWritable timed out");
            return false; // Timeout
        }
        return true;
        
#elif defined(__linux__)
        // Use epoll for waitReadable
        int evFd = ::epoll_create1(EPOLL_CLOEXEC);
        if (evFd == -1) return false;
        
        struct epoll_event epev{};
        epev.events = EPOLLIN | EPOLLERR;
        epev.data.fd = socketHandle;
        if (::epoll_ctl(evFd, EPOLL_CTL_ADD, socketHandle, &epev) == -1) {
            ::close(evFd);
            return false;
        }
        
        struct epoll_event outev{};
        int nReady = ::epoll_wait(evFd, &outev, 1, static_cast<int>(sliceMs));
        ::close(evFd);
        
        if (nReady < 0) return false;
        if (nReady == 0) {
            setError(SocketError::Timeout, "waitWritable timed out");
            return false; // Timeout
        }
        return true;
        
#elif defined(_WIN32)
        // Use WSAPoll for waitReadable
        WSAPOLLFD pfd{};
        pfd.fd = socketHandle;
        pfd.events = POLLIN;
        int nReady = ::WSAPoll(&pfd, 1, static_cast<int>(sliceMs));
        if (nReady < 0) return false;
        if (nReady == 0) {
            setError(SocketError::Timeout, "waitWritable timed out");
            return false; // Timeout
        }
        return true;
#endif
    }
}

bool SocketImpl::waitWritable(std::chrono::milliseconds timeout) {
    RETURN_IF_INVALID();
    // Use the same OS-specific polling logic as connect()
    auto deadline = std::chrono::steady_clock::now() + timeout;

    for (;;) {
        auto sliceMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        if (sliceMs < 0) return false; // Timeout

#ifdef __APPLE__
        // Use kqueue for waitWritable
        int evFd = ::kqueue();
        if (evFd == -1) return false;
        
        struct kevent reg{};
        EV_SET(&reg, static_cast<uintptr_t>(socketHandle), EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, nullptr);
        if (::kevent(evFd, &reg, 1, nullptr, 0, nullptr) == -1) {
            ::close(evFd);
            return false;
        }
        
        struct timespec ts{};
        ts.tv_sec = sliceMs / 1000;
        ts.tv_nsec = (sliceMs % 1000) * 1000000;
        
        struct kevent out{};
        int nReady = ::kevent(evFd, nullptr, 0, &out, 1, &ts);
        ::close(evFd);
        
        if (nReady < 0) return false;
        if (nReady == 0) {
            setError(SocketError::Timeout, "waitWritable timed out");
            return false; // Timeout
        }
        return true;
        
#elif defined(__linux__)
        // Use epoll for waitWritable
        int evFd = ::epoll_create1(EPOLL_CLOEXEC);
        if (evFd == -1) return false;
        
        struct epoll_event epev{};
        epev.events = EPOLLOUT | EPOLLERR;
        epev.data.fd = socketHandle;
        if (::epoll_ctl(evFd, EPOLL_CTL_ADD, socketHandle, &epev) == -1) {
            ::close(evFd);
            return false;
        }
        
        struct epoll_event outev{};
        int nReady = ::epoll_wait(evFd, &outev, 1, static_cast<int>(sliceMs));
        ::close(evFd);
        
        if (nReady < 0) return false;
        if (nReady == 0) {
            setError(SocketError::Timeout, "waitWritable timed out");
            return false; // Timeout
        }
        return true;
        
#elif defined(_WIN32)
        // Use WSAPoll for waitWritable
        WSAPOLLFD pfd{};
        pfd.fd = socketHandle;
        pfd.events = POLLOUT;
        int nReady = ::WSAPoll(&pfd, 1, static_cast<int>(sliceMs));
        if (nReady < 0) return false;
        if (nReady == 0) {
            setError(SocketError::Timeout, "waitWritable timed out");
            return false; // Timeout
        }
        return true;
#endif
    }
}

bool SocketImpl::setReceiveBufferSize(int bytes) {
    RETURN_IF_INVALID();
    return setSocketOption(socketHandle, SOL_SOCKET, SO_RCVBUF, bytes, "Failed to set SO_RCVBUF");
}

bool SocketImpl::setSendBufferSize(int bytes) {
    RETURN_IF_INVALID();
    return setSocketOption(socketHandle, SOL_SOCKET, SO_SNDBUF, bytes, "Failed to set SO_SNDBUF");
}

bool SocketImpl::shutdown(ShutdownHow how) {
    RETURN_IF_INVALID();
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
    shutdownCalled_ = true;
    SET_SUCCESS();
    return true;
}

bool SocketImpl::isBlocking() const noexcept {
    return blockingMode;
}

// -----------------------------------------------------------------------
// getLocalEndpoint / getPeerEndpoint
// -----------------------------------------------------------------------
Endpoint SocketImpl::endpointFromSockaddr(const sockaddr_storage& addr) {
    Endpoint ep;
    if (addr.ss_family == AF_INET6) {
        ep.family = AddressFamily::IPv6;
        const auto* a6
            = static_cast<const sockaddr_in6*>(static_cast<const void*>(&addr));
        ep.port = Port{ntohs(a6->sin6_port)};
        char buf[INET6_ADDRSTRLEN];
        inet_ntop(
            AF_INET6, &a6->sin6_addr, buf, static_cast<socklen_t>(sizeof(buf)));
        ep.address = buf;
    } else {
        ep.family = AddressFamily::IPv4;
        const auto* a4
            = static_cast<const sockaddr_in*>(static_cast<const void*>(&addr));
        ep.port = Port{ntohs(a4->sin_port)};
        char buf[INET_ADDRSTRLEN];
        inet_ntop(
            AF_INET, &a4->sin_addr, buf, static_cast<socklen_t>(sizeof(buf)));
        ep.address = buf;
    }
    return ep;
}

std::optional<Endpoint> SocketImpl::getLocalEndpoint() const {
    if (!isValid()) return std::nullopt;
    sockaddr_storage addr{};
    socklen_t len = static_cast<socklen_t>(sizeof(addr));
    if (getsockname(socketHandle, reinterpret_cast<sockaddr*>(&addr), &len)
        != 0)
        return std::nullopt;
    return endpointFromSockaddr(addr);
}

std::optional<Endpoint> SocketImpl::getPeerEndpoint() const {
    if (!isValid()) return std::nullopt;
    sockaddr_storage addr{};
    socklen_t len = static_cast<socklen_t>(sizeof(addr));
    if (getpeername(socketHandle, reinterpret_cast<sockaddr*>(&addr), &len)
        != 0)
        return std::nullopt;
    return endpointFromSockaddr(addr);
}

// -----------------------------------------------------------------------
// Query socket options (getters)
// -----------------------------------------------------------------------
int SocketImpl::getReceiveBufferSize() const {
    if (!isValid()) return -1;
    int size = 0;
    socklen_t len = static_cast<socklen_t>(sizeof(size));
    if (getsockopt(socketHandle, SOL_SOCKET, SO_RCVBUF, 
                   reinterpret_cast<char*>(&size), &len) != 0)
        return -1;
    return size;
}

int SocketImpl::getSendBufferSize() const {
    if (!isValid()) return -1;
    int size = 0;
    socklen_t len = static_cast<socklen_t>(sizeof(size));
    if (getsockopt(socketHandle, SOL_SOCKET, SO_SNDBUF, 
                   reinterpret_cast<char*>(&size), &len) != 0)
        return -1;
    return size;
}

bool SocketImpl::getNoDelay() const {
    if (!isValid()) return false;
    int noDelay = 0;
    socklen_t len = static_cast<socklen_t>(sizeof(noDelay));
    if (getsockopt(socketHandle, IPPROTO_TCP, TCP_NODELAY, 
                   reinterpret_cast<char*>(&noDelay), &len) != 0)
        return false;
    return noDelay != 0;
}

// Static utility methods
std::vector<NetworkInterface> SocketImpl::getLocalAddresses() {
    return aiSocks::getLocalAddresses();
}

bool SocketImpl::isValidIPv4(const std::string& address) {
    return aiSocks::isValidIPv4(address);
}

bool SocketImpl::isValidIPv6(const std::string& address) {
    return aiSocks::isValidIPv6(address);
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
