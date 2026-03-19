// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it. PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#ifdef _WIN32
#include "pch.h"
#endif
#include "SocketImpl.h"
#include "SocketImplHelpers.h"
#include "Result.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <future>
#include <thread>
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

namespace {
    // DNS lookups can block for a long time; cap detached resolver workers to
    // prevent unbounded thread growth under repeated connection attempts.
    static constexpr size_t kMaxConcurrentDnsWorkers = 4;
    static constexpr int64_t kDnsGateSleepMs = 5;
    std::atomic<size_t> g_activeDnsWorkers{0};
#ifdef AISOCKS_TESTING
    std::atomic<int64_t> g_dnsTestDelayMs{0};
#endif

    [[maybe_unused]] static bool acquireDnsWorkerSlot_(
        std::chrono::steady_clock::time_point deadline) {
        for (;;) {
            size_t cur = g_activeDnsWorkers.load(std::memory_order_relaxed);
            while (cur < kMaxConcurrentDnsWorkers) {
                if (g_activeDnsWorkers.compare_exchange_weak(cur, cur + 1,
                        std::memory_order_acq_rel, std::memory_order_relaxed))
                    return true;
            }
            if (std::chrono::steady_clock::now() >= deadline) return false;
            std::this_thread::sleep_for(
                std::chrono::milliseconds(kDnsGateSleepMs));
        }
    }

#if defined(_WIN32) && defined(AISOCKS_TESTING)
    static bool traceUnixCloseEnabled_() {
        static const bool enabled = []() {
            char buf[8] = {};
            const DWORD n
                = ::GetEnvironmentVariableA("AISOCKS_TRACE_UNIX_CLOSE", buf,
                    static_cast<DWORD>(sizeof(buf)));
            return n > 0 && buf[0] != '0';
        }();
        return enabled;
    }
#endif
} // namespace

// Platform-specific initialization
#ifdef _WIN32
bool SocketImpl::platformInit() {
    // Meyers Singleton: initialized exactly once, thread-safe since C++11.
    static const bool sWsaInitOk = []() -> bool {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return false;
        // Raise the system timer resolution to 1 ms so Sleep() and
        // deadline-based waits have fine-grained accuracy.
        timeBeginPeriod(1);
        return true;
    }();
    return sWsaInitOk;
}

void SocketImpl::platformCleanup() {
    // WSACleanup is called at process exit; timeEndPeriod mirrors
    // timeBeginPeriod to restore the default resolution.
    timeEndPeriod(1);
    WSACleanup();
}
#else
bool SocketImpl::platformInit() {
    // Suppress SIGPIPE process-wide.  Belt-and-suspenders with SO_NOSIGPIPE
    // (macOS, set per-socket) and MSG_NOSIGNAL (Linux, set per-call): this
    // catches any remaining path that bypasses those per-socket/per-call
    // guards.
    // Meyers Singleton: initialized exactly once, thread-safe since C++11.
    static const bool sInit = []() -> bool {
        ::signal(SIGPIPE, SIG_IGN);
        return true;
    }();
    (void)sInit;
    return true;
}

void SocketImpl::platformCleanup() {}
#endif

SocketImpl::SocketImpl(SocketType type, AddressFamily family)
    : socketHandle(INVALID_SOCKET_HANDLE)
    , socketType(type)
    , addressFamily(family)
    , lastError(SocketError::None)
    , blockingMode(true) {
    platformInit();

#ifdef AISOCKS_HAVE_UNIX_SOCKETS
    int af = (family == AddressFamily::Unix) ? AF_UNIX
        : (family == AddressFamily::IPv6)    ? AF_INET6
                                             : AF_INET;
#else
    int af = (family == AddressFamily::IPv6) ? AF_INET6 : AF_INET;
#endif
    int sockType = (type == SocketType::TCP) ? SOCK_STREAM : SOCK_DGRAM;
    int protocol =
#ifdef AISOCKS_HAVE_UNIX_SOCKETS
        (family == AddressFamily::Unix) ? 0 :
#endif
        (type == SocketType::TCP) ? IPPROTO_TCP
                                  : IPPROTO_UDP;

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

SocketImpl::SocketImpl()
    : socketHandle(INVALID_SOCKET_HANDLE)
    , socketType(SocketType::TCP)
    , addressFamily(AddressFamily::IPv4)
    , lastError(SocketError::InvalidSocket)
    , blockingMode(true) {}

SocketImpl::SocketImpl(
    SocketHandle handle, SocketType type, AddressFamily family)
    : socketHandle(handle)
    , socketType(type)
    , addressFamily(family)
    , lastError(SocketError::None)
    , blockingMode(true) {
    // This constructor wraps an already-open fd (e.g. from ::accept()).
    // The caller is responsible for setting the fd's blocking mode before
    // calling this constructor; SocketImpl::accept() does this explicitly.
    // We then query the kernel so blockingMode accurately reflects the fd's
    // actual state regardless of how the handle was obtained.
#ifndef _WIN32
    if (handle != INVALID_SOCKET_HANDLE) {
        int flags = ::fcntl(handle, F_GETFL, 0);
        if (flags != -1) blockingMode = (flags & O_NONBLOCK) == 0;
    }
#else
    // Windows: ioctlsocket(FIONBIO) is write-only — there is no portable way
    // to query the current state.  We rely on accept() having propagated the
    // listener's mode before constructing this object, so the blockingMode
    // set by the member-initialiser list is only a fallback for edge cases.
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
        this->close(); // so that at least isValid() will return false.
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
        this->close();
        return false;
    }

    lastError = SocketError::None;
    return true;
}

void SocketImpl::propagateSocketProps(SocketImpl& child) const {
    // Blocking mode: POSIX does not guarantee that ::accept() inherits
    // O_NONBLOCK — on Linux and macOS the returned fd is always blocking.
    // Explicitly match the listener's mode so the child is consistent.
    child.setBlocking(blockingMode);

    // Server-wide socket policies: options set once on the listening socket
    // that should apply to every accepted connection.
    int rcvBuf = getReceiveBufferSize();
    if (rcvBuf > 0) child.setReceiveBufferSize(rcvBuf);

    int sndBuf = getSendBufferSize();
    if (sndBuf > 0) child.setSendBufferSize(sndBuf);

    if (socketType == SocketType::TCP && addressFamily != AddressFamily::Unix) {
        child.setNoDelay(getNoDelay());
        child.setKeepAlive(getKeepAlive());
    }
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
            int sa_fam = reinterpret_cast<sockaddr*>(&clientAddr)->sa_family;
            AddressFamily clientFamily =
#ifdef AISOCKS_HAVE_UNIX_SOCKETS
                (sa_fam == AF_UNIX) ? AddressFamily::Unix :
#endif
                (sa_fam == AF_INET6) ? AddressFamily::IPv6
                                     : AddressFamily::IPv4;
            auto child = std::make_unique<SocketImpl>(
                clientSocket, socketType, clientFamily);
            propagateSocketProps(*child);
            lastError = SocketError::None;
            return child;
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

// ---------------------------------------------------------------------------
// waitForWritableSlice_
//
// Waits at most sliceMs milliseconds for socketHandle to become writable,
// using the platform-native event API (WSAPoll / kqueue / epoll).
// evFd is the pre-opened kqueue/epoll fd on Apple/Linux; ignored on Windows.
//
// Returns:
//   > 0  — socket is writable (caller should proceed)
//     0  — slice expired without event (caller should recheck deadline)
//    -1  — EINTR (caller should retry the slice, not count it as an error)
//   -2   — hard error; errOut contains the diagnostic string
// ---------------------------------------------------------------------------
static int waitForWritableSlice_(SocketHandle socketHandle, int evFd,
    long long sliceMs, std::string& errOut) {
#if defined(_WIN32)
    (void)evFd; // WSAPoll uses the socket handle directly; no event fd needed
    WSAPOLLFD pfd{};
    pfd.fd = socketHandle;
    pfd.events = POLLWRNORM;
    int n = ::WSAPoll(&pfd, 1, static_cast<int>(sliceMs));
    if (n < 0) {
        errOut = "WSAPoll() failed during connect";
        return -2;
    }
    return n; // 0 = timeout, 1 = writable
#elif defined(__APPLE__) || defined(__FreeBSD__)
    (void)socketHandle; // registration already done; only evFd is used here
    struct kevent out{};
    struct timespec ts{};
    ts.tv_sec = static_cast<time_t>(sliceMs / 1000);
    ts.tv_nsec = static_cast<long>((sliceMs % 1000) * 1'000'000L);
    int n = ::kevent(evFd, nullptr, 0, &out, 1, &ts);
    if (n < 0) {
        if (errno == EINTR) return -1;
        errOut = "kevent() failed during connect";
        return -2;
    }
    return n;
#elif defined(__linux__)
    (void)socketHandle; // registration already done; only evFd is used here
    struct epoll_event outev{};
    int n = ::epoll_wait(evFd, &outev, 1, static_cast<int>(sliceMs));
    if (n < 0) {
        if (errno == EINTR) return -1;
        errOut = "epoll_wait() failed during connect";
        return -2;
    }
    return n;
#else
    (void)socketHandle;
    (void)evFd;
    (void)sliceMs;
    errOut = "connect wait: unsupported platform";
    return -2;
#endif
}

// ---------------------------------------------------------------------------
// openConnectEvFd_
//
// Creates a kqueue/epoll fd and registers socketHandle for writable events.
// On Windows, evFdOut is set to -1 (WSAPoll is per-call; no persistent fd).
// Returns true on success; on failure fills errOut and returns false.
// ---------------------------------------------------------------------------
static bool openConnectEvFd_(
    SocketHandle socketHandle, int& evFdOut, std::string& errOut) {
#if defined(__APPLE__) || defined(__FreeBSD__)
    evFdOut = ::kqueue();
    if (evFdOut == -1) {
        errOut = "kqueue() failed during connect";
        return false;
    }
    struct kevent reg{};
    EV_SET(&reg, static_cast<uintptr_t>(socketHandle), EVFILT_WRITE,
        EV_ADD | EV_ENABLE, 0, 0, nullptr);
    if (::kevent(evFdOut, &reg, 1, nullptr, 0, nullptr) == -1) {
        errOut = "kevent() registration failed during connect";
        return false;
    }
    return true;
#elif defined(__linux__)
    evFdOut = ::epoll_create1(EPOLL_CLOEXEC);
    if (evFdOut == -1) {
        errOut = "epoll_create1() failed during connect";
        return false;
    }
    struct epoll_event epev{};
    epev.events = EPOLLOUT | EPOLLERR;
    epev.data.fd = socketHandle;
    if (::epoll_ctl(evFdOut, EPOLL_CTL_ADD, socketHandle, &epev) == -1) {
        errOut = "epoll_ctl() failed during connect";
        return false;
    }
    return true;
#else
    (void)socketHandle;
    (void)errOut;
    evFdOut = -1; // WSAPoll: per-call, no persistent fd needed
    return true;
#endif
}

// ---------------------------------------------------------------------------
// BlockingGuard
//
// RAII: saves and restores the OS-level blocking flag on all exit paths.
// Uses the platform API directly (not setBlocking()) so it never clobbers
// lastError — setBlocking() writes lastError=None on success.
// ---------------------------------------------------------------------------
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
        if ((savedFlags_ & O_NONBLOCK) == 0)
            fcntl(impl_.socketHandle, F_SETFL, savedFlags_ | O_NONBLOCK);
        impl_.blockingMode = false;
    }
    ~BlockingGuard() {
        fcntl(impl_.socketHandle, F_SETFL, savedFlags_);
        impl_.blockingMode = (savedFlags_ & O_NONBLOCK) == 0;
    }
#endif
};

// ---------------------------------------------------------------------------
// EvFdGuard
//
// RAII: closes the kqueue/epoll fd opened for a timed connect().
// On Windows evFd is always -1 (WSAPoll is per-call), so this is a no-op.
// ---------------------------------------------------------------------------
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
};

// Returns true when ::connect() returned an "in-progress" system error,
// meaning the non-blocking handshake has started and we should wait.
static bool isConnectInProgress_(int sysErr) noexcept {
#ifdef _WIN32
    return (sysErr == WSAEWOULDBLOCK) || (sysErr == WSAEINPROGRESS);
#else
    return (sysErr == EINPROGRESS) || (sysErr == EAGAIN);
#endif
}

// ---------------------------------------------------------------------------
// SocketImpl::resolveAddress_
//
// Phase 1 of connect(): runs getaddrinfo on a detached thread so the
// caller's timeout covers DNS as well.  The thread is detached because
// getaddrinfo cannot be cancelled; the shared_ptr keeps the result alive.
// ---------------------------------------------------------------------------
bool SocketImpl::resolveAddress_(const std::string& address, Port port,
    Milliseconds timeout, sockaddr_storage& out_addr, socklen_t& out_len) {
    struct DnsResult {
        sockaddr_storage addr{};
        socklen_t addrLen{0};
        SocketError error{SocketError::None};
        int gaiErr{0};
    };
    auto dnsRes = std::make_shared<DnsResult>();
    std::promise<void> dnsProm;
    auto dnsFut = dnsProm.get_future();
    static constexpr int64_t kDefaultDnsTimeoutMs = 5000;
    const int64_t dnsMs
        = (timeout.count > 0) ? timeout.count : kDefaultDnsTimeoutMs;
    const auto deadline
        = std::chrono::steady_clock::now() + std::chrono::milliseconds(dnsMs);

    if (!acquireDnsWorkerSlot_(deadline)) {
#ifdef _WIN32
        WSASetLastError(WSAETIMEDOUT);
#else
        errno = ETIMEDOUT;
#endif
        setError(SocketError::Timeout,
            "DNS resolution queue wait timed out for '" + address + "'");
        return false;
    }

    // We resolve on a detached worker because DNS lookups can be super-slow
    // and effectively uninterruptible; this keeps connect() timeout-bounded.
    try {
        std::thread([addr = address, p = port, af = addressFamily,
                        st = socketType, res = dnsRes,
                        prom = std::move(dnsProm)]() mutable {
            struct DnsSlotGuard {
                ~DnsSlotGuard() {
                    g_activeDnsWorkers.fetch_sub(1, std::memory_order_acq_rel);
                }
            } guard;
#ifdef AISOCKS_TESTING
            const int64_t testDelay
                = g_dnsTestDelayMs.load(std::memory_order_relaxed);
            if (testDelay > 0)
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(testDelay));
#endif
            res->error = resolveToSockaddr(addr, p, af, st,
                /*doDns=*/true, res->addr, res->addrLen, &res->gaiErr);
            prom.set_value();
        }).detach();
    } catch (...) {
        g_activeDnsWorkers.fetch_sub(1, std::memory_order_acq_rel);
        setError(SocketError::ConnectFailed, "Failed to start DNS worker");
        return false;
    }

    const auto remainingMs
        = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now())
              .count();
    if (remainingMs <= 0
        || dnsFut.wait_for(std::chrono::milliseconds(remainingMs))
            == std::future_status::timeout) {
#ifdef _WIN32
        WSASetLastError(WSAETIMEDOUT);
#else
        errno = ETIMEDOUT;
#endif
        setError(SocketError::Timeout,
            "DNS resolution timed out for '" + address + "'");
        return false;
    }

    if (dnsRes->error != SocketError::None) {
#ifdef _WIN32
        setError(SocketError::ConnectFailed,
            "Failed to resolve '" + address
                + " port:" + std::to_string(port.value()) + "'");
#else
        setErrorDns(SocketError::ConnectFailed,
            "Failed to resolve '" + address
                + " port:" + std::to_string(port.value()) + "'",
            dnsRes->gaiErr);
#endif
        return false;
    }

    out_addr = dnsRes->addr;
    out_len = dnsRes->addrLen;
    return true;
}

#ifdef AISOCKS_TESTING
size_t SocketImpl::dnsWorkerLimitForTesting() noexcept {
    return kMaxConcurrentDnsWorkers;
}

size_t SocketImpl::activeDnsWorkersForTesting() noexcept {
    return g_activeDnsWorkers.load(std::memory_order_relaxed);
}

void SocketImpl::setDnsTestDelayForTesting(Milliseconds delay) noexcept {
    g_dnsTestDelayMs.store(delay.count, std::memory_order_relaxed);
}

void SocketImpl::resetDnsTestHooksForTesting() noexcept {
    g_dnsTestDelayMs.store(0, std::memory_order_relaxed);
}
#endif

// ---------------------------------------------------------------------------
// SocketImpl::waitForConnect_
//
// Phase 3 of connect(): opens a platform event fd (kqueue/epoll; no-op on
// Windows), then loops in 100 ms slices until the handshake completes or
// the deadline expires.  Confirms success via SO_ERROR.
// ---------------------------------------------------------------------------
bool SocketImpl::waitForConnect_(Milliseconds timeout) {
    int evFd = -1;
    EvFdGuard evFdGuard{evFd};

    {
        std::string evErrMsg;
        if (!openConnectEvFd_(socketHandle, evFd, evErrMsg)) {
            setError(SocketError::ConnectFailed, std::move(evErrMsg));
            return false;
        }
    }

    return pollConnectUntilReady_(timeout, evFd);
}

bool SocketImpl::pollConnectUntilReady_(Milliseconds timeout, int evFd) {

    auto deadline = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(timeout.count);

    for (;;) {
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now())
                             .count();
        if (remaining <= 0) {
            setError(SocketError::Timeout,
                "connect() timed out after " + std::to_string(timeout.count)
                    + " ms");
            return false;
        }
        const long long sliceMs = (remaining < 100) ? remaining : 100;

        std::string evErrMsg;
        const int nReady
            = waitForWritableSlice_(socketHandle, evFd, sliceMs, evErrMsg);
        if (nReady == -1) continue; // EINTR — retry slice
        if (nReady < 0) {
            setError(SocketError::ConnectFailed, std::move(evErrMsg));
            return false;
        }
        if (nReady == 0) continue; // slice elapsed; recheck deadline

        // An event fired — confirm success via SO_ERROR.
        int sockErr = 0;
        socklen_t len = static_cast<socklen_t>(sizeof(sockErr));
        getsockopt(socketHandle, SOL_SOCKET, SO_ERROR,
            reinterpret_cast<char*>(&sockErr), &len);
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
        return true;
    }
}

bool SocketImpl::connect(
    const std::string& address, Port port, Milliseconds timeout) {
    if (!isValid()) {
        setError(SocketError::InvalidSocket, "Socket is not valid");
        return false;
    }

    sockaddr_storage serverAddr{};
    socklen_t addrLen = 0;
    if (!resolveAddress_(address, port, timeout, serverAddr, addrLen))
        return false;

    return startConnect_(serverAddr, addrLen, timeout);
}

bool SocketImpl::startConnect_(const sockaddr_storage& serverAddr,
    socklen_t addrLen, Milliseconds timeout) {

    BlockingGuard blockingGuard(*this);

    int rc = ::connect(
        socketHandle, reinterpret_cast<const sockaddr*>(&serverAddr), addrLen);
    if (rc == 0) {
        lastError = SocketError::None;
        return true;
    }

    if (!isConnectInProgress_(getLastSystemError())) {
        setError(SocketError::ConnectFailed, "Failed to connect to server");
        return false;
    }

    if (timeout.count <= 0) {
        setError(SocketError::WouldBlock,
            "connect() in progress (non-blocking socket)");
        return false;
    }

    return waitForConnect_(timeout);
}

int SocketImpl::doTransfer_(bool forSend, void* buffer, const void* data,
    size_t length, const char* timeoutMsg, const char* failMsg) {
    if (!isValid()) {
        setError(SocketError::InvalidSocket, "Socket is not valid");
        return -1;
    }

    for (;;) {
        long long ioResult = SOCKET_ERROR_CODE;
        if (forSend) {
#ifdef _WIN32
            ioResult = ::send(socketHandle, static_cast<const char*>(data),
                static_cast<int>(length), 0);
#elif defined(MSG_NOSIGNAL)
            // Linux: return EPIPE instead of raising SIGPIPE on broken
            // connection.
            ioResult = ::send(socketHandle, data, length, MSG_NOSIGNAL);
#else
            ioResult = ::send(socketHandle, data, length, 0);
#endif
        } else {
#ifdef _WIN32
            ioResult = ::recv(socketHandle, static_cast<char*>(buffer),
                static_cast<int>(length), 0);
#else
            ioResult = ::recv(socketHandle, buffer, length, 0);
#endif
        }

        if (ioResult != SOCKET_ERROR_CODE) {
            lastError = SocketError::None;
            return static_cast<int>(ioResult);
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
                setError(SocketError::Timeout, timeoutMsg);
                return -1;
            case SocketError::ConnectionReset:
                setError(
                    SocketError::ConnectionReset, "Connection reset by peer");
                return -1;
            default:
                setError(forSend ? SocketError::SendFailed
                                 : SocketError::ReceiveFailed,
                    failMsg);
                return -1;
        }
    }
}

int SocketImpl::send(const void* data, size_t length) {
    return doTransfer_(
        true, nullptr, data, length, "send() timed out", "Failed to send data");
}

int SocketImpl::receive(void* buffer, size_t length) {
    return doTransfer_(false, buffer, nullptr, length, "recv() timed out",
        "Failed to receive data");
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

// SO_REUSEADDR semantics differ critically by platform:
//   Unix:    safe — prevents TIME_WAIT stalls; still blocks concurrent binds.
//   Windows: DANGEROUS — allows multiple processes to bind the same port
//            (port hijacking). We never set it; Windows default behaviour
//            already provides exclusive binding and has negligible TIME_WAIT.
//            reuse=false uses SO_EXCLUSIVEADDRUSE for explicit enforcement.
bool SocketImpl::setReuseAddress(bool reuse) {
    RETURN_IF_INVALID();

#ifdef _WIN32
    if (!reuse) {
        int exclusive = 1;
        return setSocketOption(socketHandle, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
            exclusive, "Failed to set exclusive address use");
    }
    return true; // default Windows behaviour is already exclusively-bound
#else
    int optval = reuse ? 1 : 0;
    return setSocketOption(socketHandle, SOL_SOCKET, SO_REUSEADDR, optval,
        "Failed to set reuse address option");
#endif
}

bool SocketImpl::setTimeout(Milliseconds timeout) {
    RETURN_IF_INVALID();
    return setSocketOptionTimeout(socketHandle, SO_RCVTIMEO,
        std::chrono::milliseconds(timeout.count),
        "Failed to set receive timeout");
}

bool SocketImpl::setReceiveTimeout(Milliseconds timeout) {
    if (!isValid()) {
        return false;
    }
    return setSocketOptionTimeout(socketHandle, SO_RCVTIMEO,
        std::chrono::milliseconds(timeout.count),
        "Failed to set receive timeout");
}

void SocketImpl::close() noexcept {
    if (isValid()) {
#if defined(_WIN32) && defined(AISOCKS_TESTING)
        const bool traceUnixClose = (addressFamily == AddressFamily::Unix)
            && traceUnixCloseEnabled_();
        if (traceUnixClose) {
            std::fprintf(stderr,
                "[trace] SocketImpl::close enter handle=%llu "
                "shutdownCalled=%d\n",
                static_cast<unsigned long long>(socketHandle),
                shutdownCalled_ ? 1 : 0);
            std::fflush(stderr);
        }
#endif

        // Only call ::shutdown() if the user hasn't already done so.
        // If shutdown(Write) was called for a deliberate half-close sequence,
        // a second shutdown(RDWR) here could interfere with the FIN exchange
        // or send an RST with SO_LINGER.
        const bool shouldShutdown = !shutdownCalled_
#ifdef _WIN32
            // Windows AF_UNIX listener teardown can stall in shutdown().
            // closesocket() is sufficient to release these handles.
            && (addressFamily != AddressFamily::Unix)
#endif
            ;

        if (shouldShutdown) {
#ifdef _WIN32
#if defined(AISOCKS_TESTING)
            if (traceUnixClose) {
                std::fprintf(stderr,
                    "[trace] SocketImpl::close before shutdown(SD_BOTH) "
                    "handle=%llu\n",
                    static_cast<unsigned long long>(socketHandle));
                std::fflush(stderr);
            }
#endif
            ::shutdown(socketHandle, SD_BOTH);
#else
            ::shutdown(socketHandle, SHUT_RDWR);
#endif
        }
#ifdef _WIN32
#if defined(AISOCKS_TESTING)
        if (traceUnixClose) {
            std::fprintf(stderr,
                "[trace] SocketImpl::close before closesocket handle=%llu\n",
                static_cast<unsigned long long>(socketHandle));
            std::fflush(stderr);
        }
#endif
        closesocket(socketHandle);
#else
        ::close(socketHandle);
#endif
#if defined(_WIN32) && defined(AISOCKS_TESTING)
        if (traceUnixClose) {
            std::fprintf(stderr, "[trace] SocketImpl::close done handle=%llu\n",
                static_cast<unsigned long long>(socketHandle));
            std::fflush(stderr);
        }
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

bool SocketImpl::getLastErrorIsDns() const {
    return lastErrorIsDns;
}

int SocketImpl::getLastErrorSysCode() const {
    return lastSysCode;
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

bool SocketImpl::setSendTimeout(Milliseconds timeout) {
    RETURN_IF_INVALID();
    return setSocketOptionTimeout(socketHandle, SO_SNDTIMEO,
        std::chrono::milliseconds(timeout.count), "Failed to set send timeout");
}

bool SocketImpl::setNoDelay(bool noDelay) {
    RETURN_IF_INVALID();
    int optval = noDelay ? 1 : 0;
    return setSocketOption(socketHandle, IPPROTO_TCP, TCP_NODELAY, optval,
        "Failed to set TCP_NODELAY");
}

bool SocketImpl::setKeepAlive(bool enable) {
    RETURN_IF_INVALID();
    int optval = enable ? 1 : 0;
    return setSocketOption(socketHandle, SOL_SOCKET, SO_KEEPALIVE, optval,
        "Failed to set SO_KEEPALIVE");
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
    return setSocketOption(socketHandle, SOL_SOCKET, SO_REUSEPORT, optval,
        "Failed to set SO_REUSEPORT");
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
    return setSocketOption(socketHandle, SOL_SOCKET, SO_BROADCAST, optval,
        "Failed to set SO_BROADCAST");
}

bool SocketImpl::setMulticastTTL(int ttl) {
    RETURN_IF_INVALID();
    if (addressFamily == AddressFamily::IPv6) {
        return setSocketOption(socketHandle, IPPROTO_IPV6, IPV6_MULTICAST_HOPS,
            ttl, "Failed to set IPV6_MULTICAST_HOPS");
    } else {
        return setSocketOption(socketHandle, IPPROTO_IP, IP_MULTICAST_TTL, ttl,
            "Failed to set IP_MULTICAST_TTL");
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

bool SocketImpl::waitReady(bool forRead, std::chrono::milliseconds timeout) {
    auto deadline = std::chrono::steady_clock::now() + timeout;

    for (;;) {
        auto sliceMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now())
                           .count();
        if (sliceMs < 0) return false; // Timeout

#if defined(__APPLE__) || defined(__FreeBSD__)
        int evFd = ::kqueue();
        if (evFd == -1) return false;
        struct kevent reg{};
        int16_t filter = forRead ? EVFILT_READ : EVFILT_WRITE;
        EV_SET(&reg, static_cast<uintptr_t>(socketHandle), filter,
            EV_ADD | EV_ENABLE, 0, 0, nullptr);
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
#elif defined(__linux__)
        int evFd = ::epoll_create1(EPOLL_CLOEXEC);
        if (evFd == -1) return false;
        struct epoll_event epev{};
        epev.events = (forRead ? EPOLLIN : EPOLLOUT) | EPOLLERR;
        epev.data.fd = socketHandle;
        if (::epoll_ctl(evFd, EPOLL_CTL_ADD, socketHandle, &epev) == -1) {
            ::close(evFd);
            return false;
        }
        struct epoll_event outev{};
        int nReady = ::epoll_wait(evFd, &outev, 1, static_cast<int>(sliceMs));
        ::close(evFd);
#elif defined(_WIN32)
        WSAPOLLFD pfd{};
        pfd.fd = socketHandle;
        pfd.events = forRead ? POLLIN : POLLOUT;
        int nReady = ::WSAPoll(&pfd, 1, static_cast<int>(sliceMs));
#endif
        if (nReady < 0) return false;
        if (nReady == 0) return false; // Timeout — caller sets the error
        return true;
    }
}

bool SocketImpl::waitWithTimeoutError_(
    bool forRead, Milliseconds timeout, const char* timeoutErrorMsg) {
    RETURN_IF_INVALID();
    if (!waitReady(forRead, std::chrono::milliseconds(timeout.count))) {
        setError(SocketError::Timeout, timeoutErrorMsg);
        return false;
    }
    return true;
}

bool SocketImpl::waitReadable(Milliseconds timeout) {
    return waitWithTimeoutError_(true, timeout, "waitReadable timed out");
}

bool SocketImpl::waitWritable(Milliseconds timeout) {
    return waitWithTimeoutError_(false, timeout, "waitWritable timed out");
}

bool SocketImpl::setReceiveBufferSize(int bytes) {
    RETURN_IF_INVALID();
    return setSocketOption(
        socketHandle, SOL_SOCKET, SO_RCVBUF, bytes, "Failed to set SO_RCVBUF");
}

bool SocketImpl::setSendBufferSize(int bytes) {
    RETURN_IF_INVALID();
    return setSocketOption(
        socketHandle, SOL_SOCKET, SO_SNDBUF, bytes, "Failed to set SO_SNDBUF");
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
#ifdef AISOCKS_HAVE_UNIX_SOCKETS
    if (addr.ss_family == AF_UNIX) {
        ep.family = AddressFamily::Unix;
        ep.port = Port{0};
        const auto* un = reinterpret_cast<const sockaddr_un*>(&addr);
        ep.address = un->sun_path; // may be empty for anonymous sockets
        return ep;
    }
#endif // AISOCKS_HAVE_UNIX_SOCKETS
    if (addr.ss_family == AF_INET6) {
        ep.family = AddressFamily::IPv6;
        const auto* a6 = reinterpret_cast<const sockaddr_in6*>(&addr);
        ep.port = Port{ntohs(a6->sin6_port)};
        char buf[INET6_ADDRSTRLEN];
        inet_ntop(
            AF_INET6, &a6->sin6_addr, buf, static_cast<socklen_t>(sizeof(buf)));
        ep.address = buf;
    } else {
        ep.family = AddressFamily::IPv4;
        const auto* a4 = reinterpret_cast<const sockaddr_in*>(&addr);
        ep.port = Port{ntohs(a4->sin_port)};
        char buf[INET_ADDRSTRLEN];
        inet_ntop(
            AF_INET, &a4->sin_addr, buf, static_cast<socklen_t>(sizeof(buf)));
        ep.address = buf;
    }
    return ep;
}

Result<Endpoint> SocketImpl::getLocalEndpoint() const {
    if (!isValid())
        return Result<Endpoint>::failure(
            SocketError::InvalidSocket, "getLocalEndpoint", 0, false);
    sockaddr_storage addr{};
    socklen_t len = static_cast<socklen_t>(sizeof(addr));
    if (::getsockname(socketHandle, reinterpret_cast<sockaddr*>(&addr), &len)
        != 0) {
        return Result<Endpoint>::failure(
            getLastError(), "getsockname", 0, false);
    }
    return Result<Endpoint>::success(endpointFromSockaddr(addr));
}

Result<Endpoint> SocketImpl::getPeerEndpoint() const {
    if (!isValid())
        return Result<Endpoint>::failure(
            SocketError::InvalidSocket, "getPeerEndpoint", 0, false);
    sockaddr_storage addr{};
    socklen_t len = static_cast<socklen_t>(sizeof(addr));
    if (::getpeername(socketHandle, reinterpret_cast<sockaddr*>(&addr), &len)
        != 0) {
        return Result<Endpoint>::failure(
            getLastError(), "getpeername", 0, false);
    }
    return Result<Endpoint>::success(endpointFromSockaddr(addr));
}

// -----------------------------------------------------------------------
// Query socket options (getters)
// -----------------------------------------------------------------------
int SocketImpl::getReceiveBufferSize() const {
    if (!isValid()) return -1;
    int size = 0;
    socklen_t len = static_cast<socklen_t>(sizeof(size));
    if (getsockopt(socketHandle, SOL_SOCKET, SO_RCVBUF,
            reinterpret_cast<char*>(&size), &len)
        != 0)
        return -1;
    return size;
}

int SocketImpl::getSendBufferSize() const {
    if (!isValid()) return -1;
    int size = 0;
    socklen_t len = static_cast<socklen_t>(sizeof(size));
    if (getsockopt(socketHandle, SOL_SOCKET, SO_SNDBUF,
            reinterpret_cast<char*>(&size), &len)
        != 0)
        return -1;
    return size;
}

bool SocketImpl::getNoDelay() const {
    if (!isValid()) return false;
    int noDelay = 0;
    socklen_t len = static_cast<socklen_t>(sizeof(noDelay));
    if (getsockopt(socketHandle, IPPROTO_TCP, TCP_NODELAY,
            reinterpret_cast<char*>(&noDelay), &len)
        != 0)
        return false;
    return noDelay != 0;
}

bool SocketImpl::getKeepAlive() const {
    if (!isValid()) return false;
    int keepAlive = 0;
    socklen_t len = static_cast<socklen_t>(sizeof(keepAlive));
    if (getsockopt(socketHandle, SOL_SOCKET, SO_KEEPALIVE,
            reinterpret_cast<char*>(&keepAlive), &len)
        != 0)
        return false;
    return keepAlive != 0;
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
