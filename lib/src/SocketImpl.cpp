// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
#ifdef _WIN32
#include "pch.h"
#endif
#include "SocketImpl.h"
#include <chrono>
#include <cstring>
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

// -----------------------------------------------------------------------
// Static helpers (file-scope)
// -----------------------------------------------------------------------

// Classify the errno / WSAError from a send/recv syscall into a SocketError
// value.  Does NO string work \u2014 descriptions are provided by the caller so
// zero allocation occurs even in the WouldBlock fast path.
static SocketError classifyTransferSysError(int sysErr) noexcept {
#ifdef _WIN32
    if (sysErr == WSAEWOULDBLOCK) return SocketError::WouldBlock;
    if (sysErr == WSAETIMEDOUT) return SocketError::Timeout;
    if (sysErr == WSAECONNRESET || sysErr == WSAECONNABORTED)
        return SocketError::ConnectionReset;
#else
    if (sysErr == EWOULDBLOCK || sysErr == EAGAIN)
        return SocketError::WouldBlock;
    if (sysErr == ETIMEDOUT) return SocketError::Timeout;
    if (sysErr == ECONNRESET || sysErr == EPIPE)
        return SocketError::ConnectionReset;
#endif
    return SocketError::Unknown;
}

// Fill `out`/`outLen` from a literal address string or (when doDns=true) a
// DNS lookup.  Wildcards ("", "0.0.0.0", "::") map to INADDR_ANY/in6addr_any.
// Returns SocketError::None on success.  On DNS failure *gaiErr is set to the
// EAI_* code and ConnectFailed is returned.  On literal-parse failure with
// doDns=false, BindFailed is returned.
static SocketError resolveToSockaddr(const std::string& address, Port port,
    AddressFamily family, SocketType sockType, bool doDns,
    sockaddr_storage& out, socklen_t& outLen, int* gaiErr = nullptr) {
    if (family == AddressFamily::IPv6) {
        sockaddr_in6 a6{};
        a6.sin6_family = AF_INET6;
        a6.sin6_port = htons(port);
        if (address.empty() || address == "::" || address == "0.0.0.0") {
            a6.sin6_addr = in6addr_any;
        } else if (inet_pton(AF_INET6, address.c_str(), &a6.sin6_addr) > 0) {
            // literal parsed OK
        } else if (doDns) {
            struct addrinfo hints{}, *res = nullptr;
            hints.ai_family = AF_INET6;
            hints.ai_socktype
                = (sockType == SocketType::TCP) ? SOCK_STREAM : SOCK_DGRAM;
            int gai = getaddrinfo(address.c_str(), nullptr, &hints, &res);
            if (gai != 0) {
                if (gaiErr) *gaiErr = gai;
                return SocketError::ConnectFailed;
            }
            std::memcpy(
                &a6, res->ai_addr, static_cast<size_t>(res->ai_addrlen));
            a6.sin6_port = htons(port);
            freeaddrinfo(res);
        } else {
            return SocketError::BindFailed;
        }
        std::memset(&out, 0, sizeof(out));
        std::memcpy(&out, &a6, sizeof(a6));
        outLen = static_cast<socklen_t>(sizeof(sockaddr_in6));
    } else {
        sockaddr_in a4{};
        a4.sin_family = AF_INET;
        a4.sin_port = htons(port);
        if (address.empty() || address == "0.0.0.0") {
            a4.sin_addr.s_addr = INADDR_ANY;
        } else if (inet_pton(AF_INET, address.c_str(), &a4.sin_addr) > 0) {
            // literal parsed OK
        } else if (doDns) {
            struct addrinfo hints{}, *res = nullptr;
            hints.ai_family = AF_INET;
            hints.ai_socktype
                = (sockType == SocketType::TCP) ? SOCK_STREAM : SOCK_DGRAM;
            int gai = getaddrinfo(address.c_str(), nullptr, &hints, &res);
            if (gai != 0) {
                if (gaiErr) *gaiErr = gai;
                return SocketError::ConnectFailed;
            }
            std::memcpy(
                &a4, res->ai_addr, static_cast<size_t>(res->ai_addrlen));
            a4.sin_port = htons(port);
            freeaddrinfo(res);
        } else {
            return SocketError::BindFailed;
        }
        std::memset(&out, 0, sizeof(out));
        std::memcpy(&out, &a4, sizeof(a4));
        outLen = static_cast<socklen_t>(sizeof(sockaddr_in));
    }
    return SocketError::None;
}

// Platform-specific initialization
#ifdef _WIN32
bool SocketImpl::platformInit() {
    static bool initialized = []() {
        WSADATA wsaData;
        bool wsaOk = (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0);
        if (wsaOk) {
            timeBeginPeriod(1);
        }
        return wsaOk;
    }();
    return initialized;
}

void SocketImpl::platformCleanup() {
    // Note: With Meyers singleton, we can't reliably cleanup
    // as we don't know if initialization occurred
    // This is a known limitation of Meyers singleton pattern
}
#else
bool SocketImpl::platformInit() {
    // Suppress SIGPIPE process-wide.  Belt-and-suspenders with SO_NOSIGPIPE
    // (macOS, set per-socket) and MSG_NOSIGNAL (Linux, set per-call): this
    // catches any remaining path that bypasses those per-socket/per-call
    // guards.
    static bool initialized = []() {
        ::signal(SIGPIPE, SIG_IGN);
        return true;
    }();
    (void)initialized; // Suppress unused variable warning
    return true;
}

void SocketImpl::platformCleanup() {
    // No cleanup needed for SIGPIPE handling
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

bool SocketImpl::isBlocking() const noexcept {
    return blockingMode;
}

// -----------------------------------------------------------------------
// Private setsockopt helpers
// -----------------------------------------------------------------------

bool SocketImpl::setBoolOpt(
    int level, int optname, bool val, const char* errMsg) {
    int optval = val ? 1 : 0;
    if (setsockopt(socketHandle, level, optname,
            reinterpret_cast<const char*>(&optval),
            static_cast<socklen_t>(sizeof(optval)))
        == SOCKET_ERROR_CODE) {
        setError(SocketError::SetOptionFailed, errMsg);
        return false;
    }
    lastError = SocketError::None;
    return true;
}

bool SocketImpl::setTimeoutOpt(
    int optname, std::chrono::milliseconds timeout, const char* errMsg) {
    const long long ms = timeout.count();
#ifdef _WIN32
    DWORD tv = static_cast<DWORD>(ms);
    if (setsockopt(socketHandle, SOL_SOCKET, optname,
            reinterpret_cast<const char*>(&tv),
            static_cast<socklen_t>(sizeof(tv)))
        == SOCKET_ERROR_CODE) {
        setError(SocketError::SetOptionFailed, errMsg);
        return false;
    }
#else
    struct timeval tv;
    tv.tv_sec = static_cast<decltype(tv.tv_sec)>(ms / 1000);
    tv.tv_usec = static_cast<decltype(tv.tv_usec)>((ms % 1000) * 1000);
    if (setsockopt(socketHandle, SOL_SOCKET, optname, &tv,
            static_cast<socklen_t>(sizeof(tv)))
        == SOCKET_ERROR_CODE) {
        setError(SocketError::SetOptionFailed, errMsg);
        return false;
    }
#endif
    lastError = SocketError::None;
    return true;
}

bool SocketImpl::setBufSizeOpt(int optname, int bytes, const char* errMsg) {
    if (setsockopt(socketHandle, SOL_SOCKET, optname,
            reinterpret_cast<const char*>(&bytes),
            static_cast<socklen_t>(sizeof(bytes)))
        == SOCKET_ERROR_CODE) {
        setError(SocketError::SetOptionFailed, errMsg);
        return false;
    }
    lastError = SocketError::None;
    return true;
}

bool SocketImpl::setReuseAddress(bool reuse) {
    if (!isValid()) {
        setError(SocketError::InvalidSocket, "Socket is not valid");
        return false;
    }
    return setBoolOpt(
        SOL_SOCKET, SO_REUSEADDR, reuse, "Failed to set reuse address option");
}

bool SocketImpl::setTimeout(std::chrono::milliseconds timeout) {
    if (!isValid()) {
        setError(SocketError::InvalidSocket, "Socket is not valid");
        return false;
    }
    return setTimeoutOpt(SO_RCVTIMEO, timeout, "Failed to set receive timeout");
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

std::string formatErrorContext(const ErrorContext& ctx) {
    std::string sysText;
#ifdef _WIN32
    (void)ctx.isDns; // Windows: FormatMessage handles all codes (errno + EAI_*)
    char buf[512] = {};
    DWORD len = FormatMessageA(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
        static_cast<DWORD>(ctx.sysCode), 0, buf,
        static_cast<DWORD>(sizeof(buf)), nullptr);
    while (len > 0 && (buf[len - 1] == '\r' || buf[len - 1] == '\n'))
        buf[--len] = '\0';
    sysText = buf;
#else
    sysText = ctx.isDns ? ::gai_strerror(ctx.sysCode) : ::strerror(ctx.sysCode);
#endif
    // Avoid <sstream> (pulls in wide-string instantiations that add ~100 ms
    // of template instantiation cost across every TU).  Plain concatenation
    // produces the same output with zero overhead.
    std::string result;
    result.reserve(128);
    if (ctx.description) result += ctx.description;
    result += " [";
    result += std::to_string(ctx.sysCode);
    result += ": ";
    result += sysText;
    result += "]";
    return result;
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

// -----------------------------------------------------------------------
// setSendTimeout / setNoDelay / setKeepAlive / setLingerAbort / setReusePort
// -----------------------------------------------------------------------
bool SocketImpl::setSendTimeout(std::chrono::milliseconds timeout) {
    if (!isValid()) {
        setError(SocketError::InvalidSocket, "Socket is not valid");
        return false;
    }
    return setTimeoutOpt(SO_SNDTIMEO, timeout, "Failed to set send timeout");
}

bool SocketImpl::setNoDelay(bool noDelay) {
    if (!isValid()) {
        setError(SocketError::InvalidSocket, "Socket is not valid");
        return false;
    }
    return setBoolOpt(
        IPPROTO_TCP, TCP_NODELAY, noDelay, "Failed to set TCP_NODELAY");
}

bool SocketImpl::setKeepAlive(bool enable) {
    if (!isValid()) {
        setError(SocketError::InvalidSocket, "Socket is not valid");
        return false;
    }
    return setBoolOpt(
        SOL_SOCKET, SO_KEEPALIVE, enable, "Failed to set SO_KEEPALIVE");
}

// -----------------------------------------------------------------------
// setLingerAbort (SO_LINGER, l_linger=0  RST on close)
// -----------------------------------------------------------------------
bool SocketImpl::setLingerAbort(bool enable) {
    if (!isValid()) {
        setError(SocketError::InvalidSocket, "Socket is not valid");
        return false;
    }
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
    lastError = SocketError::None;
    return true;
}

bool SocketImpl::setReusePort(bool enable) {
    if (!isValid()) {
        setError(SocketError::InvalidSocket, "Socket is not valid");
        return false;
    }
#ifdef SO_REUSEPORT
    return setBoolOpt(
        SOL_SOCKET, SO_REUSEPORT, enable, "Failed to set SO_REUSEPORT");
#else
    (void)enable;
    setError(SocketError::SetOptionFailed,
        "SO_REUSEPORT is not supported on this platform");
    return false;
#endif
}

// -----------------------------------------------------------------------
// setBroadcast (SO_BROADCAST)  required before sending to broadcast addrs
// -----------------------------------------------------------------------
bool SocketImpl::setBroadcast(bool enable) {
    if (!isValid()) {
        setError(SocketError::InvalidSocket, "Socket is not valid");
        return false;
    }
    return setBoolOpt(
        SOL_SOCKET, SO_BROADCAST, enable, "Failed to set SO_BROADCAST");
}

// -----------------------------------------------------------------------
// setMulticastTTL (IP_MULTICAST_TTL / IPV6_MULTICAST_HOPS)  limit multicast hops
// -----------------------------------------------------------------------
bool SocketImpl::setMulticastTTL(int ttl) {
    if (!isValid()) {
        setError(SocketError::InvalidSocket, "Socket is not valid");
        return false;
    }
    if (addressFamily == AddressFamily::IPv6) {
#ifdef _WIN32
        return setBoolOpt(IPPROTO_IPV6, IPV6_MULTICAST_HOPS, ttl, 
                         "Failed to set IPV6_MULTICAST_HOPS");
#else
        return setBoolOpt(IPPROTO_IPV6, IPV6_MULTICAST_HOPS, ttl, 
                         "Failed to set IPV6_MULTICAST_HOPS");
#endif
    } else {
        return setBoolOpt(IPPROTO_IP, IP_MULTICAST_TTL, ttl, 
                         "Failed to set IP_MULTICAST_TTL");
    }
}

// -----------------------------------------------------------------------
// sendAll  loop until all bytes sent or error
// -----------------------------------------------------------------------
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

// receiveAll  loop until all bytes received, error, or EOF
// -----------------------------------------------------------------------
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

// -----------------------------------------------------------------------
// waitReadable / waitWritable  single-fd select convenience
// -----------------------------------------------------------------------
bool SocketImpl::waitReady(bool forRead, std::chrono::milliseconds timeout) {
    if (!isValid()) {
        setError(SocketError::InvalidSocket, "Socket is not valid");
        return false;
    }
    auto deadline = std::chrono::steady_clock::now() + timeout;

    for (;;) {
        auto rem = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now())
                       .count();
        if (rem < 0) rem = 0;

        struct timeval tv;
        tv.tv_sec = static_cast<decltype(tv.tv_sec)>(rem / 1000);
        tv.tv_usec = static_cast<decltype(tv.tv_usec)>((rem % 1000) * 1000);

        fd_set fdSet;
        FD_ZERO(&fdSet);
        FD_SET(socketHandle, &fdSet);
        fd_set* rd = forRead ? &fdSet : nullptr;
        fd_set* wr = !forRead ? &fdSet : nullptr;

        int sel = ::select(
            static_cast<int>(socketHandle) + 1, rd, wr, nullptr, &tv);
        if (sel < 0) {
#ifndef _WIN32
            if (errno == EINTR) continue;
#endif
            setError(SocketError::Unknown,
                forRead ? "select() failed in waitReadable"
                        : "select() failed in waitWritable");
            return false;
        }
        if (sel == 0) {
            setError(SocketError::Timeout,
                forRead ? "waitReadable() timed out"
                        : "waitWritable() timed out");
            return false;
        }
        lastError = SocketError::None;
        return true;
    }
}

bool SocketImpl::waitReadable(std::chrono::milliseconds timeout) {
    return waitReady(true, timeout);
}

bool SocketImpl::waitWritable(std::chrono::milliseconds timeout) {
    return waitReady(false, timeout);
}

// -----------------------------------------------------------------------
// setReceiveBufferSize / setSendBufferSize (SO_RCVBUF / SO_SNDBUF)
// -----------------------------------------------------------------------
bool SocketImpl::setReceiveBufferSize(int bytes) {
    if (!isValid()) {
        setError(SocketError::InvalidSocket, "Socket is not valid");
        return false;
    }
    return setBufSizeOpt(SO_RCVBUF, bytes, "Failed to set SO_RCVBUF");
}

bool SocketImpl::setSendBufferSize(int bytes) {
    if (!isValid()) {
        setError(SocketError::InvalidSocket, "Socket is not valid");
        return false;
    }
    return setBufSizeOpt(SO_SNDBUF, bytes, "Failed to set SO_SNDBUF");
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
    shutdownCalled_ = true;
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
            // Use IFF_LOOPBACK (POSIX)  reliable on macOS (lo0) and Linux (lo).
            const bool isLo = (ifa->ifa_flags & IFF_LOOPBACK) != 0;
            if (family == AF_INET) {
                char buffer[INET_ADDRSTRLEN];
                sockaddr_in* sin
                    = reinterpret_cast<sockaddr_in*>(ifa->ifa_addr);
                inet_ntop(AF_INET, &sin->sin_addr, buffer, INET_ADDRSTRLEN);
                iface.address = buffer;
                iface.family = AddressFamily::IPv4;
                iface.isLoopback = isLo;
                interfaces.push_back(iface);
            } else if (family == AF_INET6) {
                char buffer[INET6_ADDRSTRLEN];
                sockaddr_in6* sin6
                    = reinterpret_cast<sockaddr_in6*>(ifa->ifa_addr);
                inet_ntop(AF_INET6, &sin6->sin6_addr, buffer, INET6_ADDRSTRLEN);
                iface.address = buffer;
                iface.family = AddressFamily::IPv6;
                iface.isLoopback = isLo;
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
