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
#include <algorithm>
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

// ---------------------------------------------------------------------------
// BlockingGuard implementation
// ---------------------------------------------------------------------------
#ifdef _WIN32
SocketImpl::BlockingGuard::BlockingGuard(SocketImpl& impl)
    : impl_(impl), wasBlocking_(impl.isBlocking()) {
    if (wasBlocking_) {
        u_long nb = 1;
        ioctlsocket(impl_.socketHandle, FIONBIO, &nb);
        impl_.blockingMode = false;
    }
}

SocketImpl::BlockingGuard::~BlockingGuard() {
    if (wasBlocking_) {
        u_long blk = 0;
        ioctlsocket(impl_.socketHandle, FIONBIO, &blk);
        impl_.blockingMode = true;
    }
}
#else
SocketImpl::BlockingGuard::BlockingGuard(SocketImpl& impl)
    : impl_(impl), savedFlags_(fcntl(impl.socketHandle, F_GETFL, 0)) {
    if ((savedFlags_ & O_NONBLOCK) == 0)
        fcntl(impl_.socketHandle, F_SETFL, savedFlags_ | O_NONBLOCK);
    impl_.blockingMode = false;
}

SocketImpl::BlockingGuard::~BlockingGuard() {
    fcntl(impl_.socketHandle, F_SETFL, savedFlags_);
    impl_.blockingMode = (savedFlags_ & O_NONBLOCK) == 0;
}
#endif

// ---------------------------------------------------------------------------
// EvFdGuard implementation
// ---------------------------------------------------------------------------
SocketImpl::EvFdGuard::~EvFdGuard() {
#if !defined(_WIN32)
    if (fd_ != -1) {
        ::close(fd_);
        fd_ = -1;
    }
#endif
}

namespace {
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
#define AISOCKS_INTERNAL_CALL
    child.setBlocking(blockingMode);
#undef AISOCKS_INTERNAL_CALL

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
        const auto* a6 = reinterpret_cast<const sockaddr_in6*>(
            static_cast<const void*>(&addr));
        ep.port = Port{ntohs(a6->sin6_port)};
        char buf[INET6_ADDRSTRLEN];
        inet_ntop(
            AF_INET6, &a6->sin6_addr, buf, static_cast<socklen_t>(sizeof(buf)));
        ep.address = buf;
    } else {
        ep.family = AddressFamily::IPv4;
        const auto* a4 = reinterpret_cast<const sockaddr_in*>(
            static_cast<const void*>(&addr));
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
static bool getSockOptInt_(
    SocketHandle handle, int level, int optname, int& out) {
    out = 0;
    socklen_t len = static_cast<socklen_t>(sizeof(out));
    return getsockopt(
               handle, level, optname, reinterpret_cast<char*>(&out), &len)
        == 0;
}

int SocketImpl::getReceiveBufferSize() const {
    if (!isValid()) return -1;
    int size = 0;
    if (!getSockOptInt_(socketHandle, SOL_SOCKET, SO_RCVBUF, size)) return -1;
    return size;
}

int SocketImpl::getSendBufferSize() const {
    if (!isValid()) return -1;
    int size = 0;
    if (!getSockOptInt_(socketHandle, SOL_SOCKET, SO_SNDBUF, size)) return -1;
    return size;
}

bool SocketImpl::getNoDelay() const {
    if (!isValid()) return false;
    int noDelay = 0;
    if (!getSockOptInt_(socketHandle, IPPROTO_TCP, TCP_NODELAY, noDelay))
        return false;
    return noDelay != 0;
}

bool SocketImpl::getKeepAlive() const {
    if (!isValid()) return false;
    int keepAlive = 0;
    if (!getSockOptInt_(socketHandle, SOL_SOCKET, SO_KEEPALIVE, keepAlive))
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
