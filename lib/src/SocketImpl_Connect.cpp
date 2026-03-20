// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
// 
// Socket implementation: Connection handshakes and DNS resolution.
#ifdef _WIN32
#include "pch.h"
#endif
#include "SocketImpl.h"
#include "SocketImplHelpers.h"
#include <atomic>
#include <chrono>
#include <future>
#include <thread>

#if defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/event.h>
#elif defined(__linux__)
#include <sys/epoll.h>
#endif

namespace aiSocks {

namespace {
    static constexpr size_t kMaxConcurrentDnsWorkers = 4;
    static constexpr int64_t kDnsGateSleepMs = 5;
    std::atomic<size_t> g_activeDnsWorkers{0};
#ifdef AISOCKS_TESTING
    std::atomic<int64_t> g_dnsTestDelayMs{0};
#endif

    static bool acquireDnsWorkerSlot_(std::chrono::steady_clock::time_point deadline) {
        for (;;) {
            size_t cur = g_activeDnsWorkers.load(std::memory_order_relaxed);
            while (cur < kMaxConcurrentDnsWorkers) {
                if (g_activeDnsWorkers.compare_exchange_weak(cur, cur + 1,
                        std::memory_order_acq_rel, std::memory_order_relaxed))
                    return true;
            }
            if (std::chrono::steady_clock::now() >= deadline) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(kDnsGateSleepMs));
        }
    }

    static void releaseDnsWorkerSlot_() noexcept {
        g_activeDnsWorkers.fetch_sub(1, std::memory_order_acq_rel);
    }

    struct DnsSlotGuard {
        ~DnsSlotGuard() { releaseDnsWorkerSlot_(); }
    };

    static int waitForWritableSlice_(SocketHandle socketHandle, int evFd,
        long long sliceMs, std::string& errOut) {
#if defined(_WIN32)
        (void)evFd;
        WSAPOLLFD pfd{};
        pfd.fd = socketHandle;
        pfd.events = POLLWRNORM;
        int n = ::WSAPoll(&pfd, 1, static_cast<int>(sliceMs));
        if (n < 0) {
            errOut = "WSAPoll() failed during connect";
            return -2;
        }
        return n;
#elif defined(__APPLE__) || defined(__FreeBSD__)
        (void)socketHandle;
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
        (void)socketHandle;
        struct epoll_event outev{};
        int n = ::epoll_wait(evFd, &outev, 1, static_cast<int>(sliceMs));
        if (n < 0) {
            if (errno == EINTR) return -1;
            errOut = "epoll_wait() failed during connect";
            return -2;
        }
        return n;
#else
        (void)socketHandle; (void)evFd; (void)sliceMs;
        errOut = "connect wait: unsupported platform";
        return -2;
#endif
    }

    static bool openConnectEvFd_(SocketHandle socketHandle, int& evFdOut, std::string& errOut) {
#if defined(__APPLE__) || defined(__FreeBSD__)
        evFdOut = ::kqueue();
        if (evFdOut == -1) {
            errOut = "kqueue() failed during connect";
            return false;
        }
        struct kevent reg{};
        EV_SET(&reg, static_cast<uintptr_t>(socketHandle), EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, nullptr);
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
        (void)socketHandle; (void)errOut;
        evFdOut = -1;
        return true;
#endif
    }

    static bool isConnectInProgress_(int sysErr) noexcept {
#ifdef _WIN32
        return (sysErr == WSAEWOULDBLOCK) || (sysErr == WSAEINPROGRESS);
#else
        return (sysErr == EINPROGRESS) || (sysErr == EAGAIN);
#endif
    }
} // namespace

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
    const int64_t dnsMs = (timeout.count > 0) ? timeout.count : kDefaultDnsTimeoutMs;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(dnsMs);

    if (!acquireDnsWorkerSlot_(deadline)) {
#ifdef _WIN32
        WSASetLastError(WSAETIMEDOUT);
#else
        errno = ETIMEDOUT;
#endif
        setError(SocketError::Timeout, "DNS resolution queue wait timed out for '" + address + "'");
        return false;
    }

    try {
        std::thread([addr = address, p = port, af = addressFamily, st = socketType, res = dnsRes, prom = std::move(dnsProm)]() mutable {
            DnsSlotGuard guard;
#ifdef AISOCKS_TESTING
            const int64_t testDelay = g_dnsTestDelayMs.load(std::memory_order_relaxed);
            if (testDelay > 0) std::this_thread::sleep_for(std::chrono::milliseconds(testDelay));
#endif
            res->error = resolveToSockaddr(addr, p, af, st, /*doDns=*/true, res->addr, res->addrLen, &res->gaiErr);
            prom.set_value();
        }).detach();
    } catch (...) {
        releaseDnsWorkerSlot_();
        setError(SocketError::ConnectFailed, "Failed to start DNS worker");
        return false;
    }

    const auto remainingMs = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
    if (remainingMs <= 0 || dnsFut.wait_for(std::chrono::milliseconds(remainingMs)) == std::future_status::timeout) {
#ifdef _WIN32
        WSASetLastError(WSAETIMEDOUT);
#else
        errno = ETIMEDOUT;
#endif
        setError(SocketError::Timeout, "DNS resolution timed out for '" + address + "'");
        return false;
    }

    if (dnsRes->error != SocketError::None) {
#ifdef _WIN32
        setError(SocketError::ConnectFailed, "Failed to resolve '" + address + " port:" + std::to_string(port.value()) + "'");
#else
        setErrorDns(SocketError::ConnectFailed, "Failed to resolve '" + address + " port:" + std::to_string(port.value()) + "'", dnsRes->gaiErr);
#endif
        return false;
    }

    out_addr = dnsRes->addr;
    out_len = dnsRes->addrLen;
    return true;
}

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
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout.count);
    for (;;) {
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (remaining <= 0) {
            setError(SocketError::Timeout, "connect() timed out after " + std::to_string(timeout.count) + " ms");
            return false;
        }
        const long long sliceMs = (remaining < 100) ? remaining : 100;
        std::string evErrMsg;
        const int nReady = waitForWritableSlice_(socketHandle, evFd, sliceMs, evErrMsg);
        if (nReady == -1) continue; 
        if (nReady < 0) { setError(SocketError::ConnectFailed, std::move(evErrMsg)); return false; }
        if (nReady == 0) continue; 

        int sockErr = 0;
        socklen_t len = static_cast<socklen_t>(sizeof(sockErr));
        getsockopt(socketHandle, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&sockErr), &len);
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

bool SocketImpl::connect(const std::string& address, Port port, Milliseconds timeout) {
    if (!isValid()) { setError(SocketError::InvalidSocket, "Socket is not valid"); return false; }
    sockaddr_storage serverAddr{};
    socklen_t addrLen = 0;
    if (!resolveAddress_(address, port, timeout, serverAddr, addrLen)) return false;
    return startConnect_(serverAddr, addrLen, timeout);
}

bool SocketImpl::startConnect_(const sockaddr_storage& serverAddr, socklen_t addrLen, Milliseconds timeout) {
    BlockingGuard blockingGuard(*this);
    int rc = ::connect(socketHandle, reinterpret_cast<const sockaddr*>(&serverAddr), addrLen);
    if (rc == 0) { lastError = SocketError::None; return true; }
    if (!isConnectInProgress_(getLastSystemError())) {
        setError(SocketError::ConnectFailed, "Failed to connect to server");
        return false;
    }
    if (timeout.count <= 0) {
        setError(SocketError::WouldBlock, "connect() in progress (non-blocking socket)");
        return false;
    }
    return waitForConnect_(timeout);
}

#ifdef AISOCKS_TESTING
size_t SocketImpl::dnsWorkerLimitForTesting() noexcept { return kMaxConcurrentDnsWorkers; }
size_t SocketImpl::activeDnsWorkersForTesting() noexcept { return g_activeDnsWorkers.load(std::memory_order_relaxed); }
void SocketImpl::setDnsTestDelayForTesting(Milliseconds delay) noexcept { g_dnsTestDelayMs.store(delay.count, std::memory_order_relaxed); }
void SocketImpl::resetDnsTestHooksForTesting() noexcept { g_dnsTestDelayMs.store(0, std::memory_order_relaxed); }
#endif

} // namespace aiSocks
