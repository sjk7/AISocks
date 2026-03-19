// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it. PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// SocketImplHelpers.cpp - Helper functions for SocketImpl
// Extracted for better code organization

#include "SocketImplHelpers.h"
#include "SocketImpl.h"
#include <cstring>
#include <vector>
#include <cassert>

#ifdef _WIN32
#include <winsock2.h>
#include <iphlpapi.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <net/if.h>
#endif
#ifdef AISOCKS_HAVE_UNIX_SOCKETS
#ifdef _WIN32
#include <afunix.h>
#else
#include <sys/un.h>
#endif
#endif

namespace aiSocks {

// -----------------------------------------------------------------------
// Error classification helper
// -----------------------------------------------------------------------

// Classify the errno / WSAError from a send/recv syscall into a SocketError
// value. Does NO string work -- descriptions are provided by the caller so
// zero allocation occurs even in the WouldBlock fast path.
SocketError classifyTransferSysError(int sysErr) noexcept {
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

// -----------------------------------------------------------------------
// Address resolution helper
// -----------------------------------------------------------------------

static SocketError resolveIPv6_(const std::string& address, Port port,
    SocketType sockType, bool doDns, sockaddr_storage& out, socklen_t& outLen,
    int* gaiErr) {
    sockaddr_in6 a6{};
    a6.sin6_family = AF_INET6;
    a6.sin6_port = htons(port.value());
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
        memcpy(&a6, res->ai_addr, static_cast<size_t>(res->ai_addrlen));
        a6.sin6_port = htons(port.value());
        freeaddrinfo(res);
    } else {
        return SocketError::BindFailed;
    }
    memset(&out, 0, sizeof(out));
    memcpy(&out, &a6, sizeof(a6));
    outLen = static_cast<socklen_t>(sizeof(sockaddr_in6));
    return SocketError::None;
}

static SocketError resolveIPv4_(const std::string& address, Port port,
    SocketType sockType, bool doDns, sockaddr_storage& out, socklen_t& outLen,
    int* gaiErr) {
    sockaddr_in a4{};
    a4.sin_family = AF_INET;
    a4.sin_port = htons(port.value());
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
        memcpy(&a4, res->ai_addr, static_cast<size_t>(res->ai_addrlen));
        a4.sin_port = htons(port.value());
        freeaddrinfo(res);
    } else {
        return SocketError::BindFailed;
    }
    memset(&out, 0, sizeof(out));
    memcpy(&out, &a4, sizeof(a4));
    outLen = static_cast<socklen_t>(sizeof(sockaddr_in));
    return SocketError::None;
}

// Fill `out`/`outLen` from a literal address string or (when doDns=true) a
// DNS lookup. Wildcards ("", "0.0.0.0", "::") map to INADDR_ANY/in6addr_any.
// Returns SocketError::None on success. On DNS failure *gaiErr is set to the
// EAI_* code and ConnectFailed is returned. On literal-parse failure with
// doDns=false, BindFailed is returned.
SocketError resolveToSockaddr(const std::string& address, Port port,
    AddressFamily family, SocketType sockType, bool doDns,
    sockaddr_storage& out, socklen_t& outLen, int* gaiErr) {
#ifdef AISOCKS_HAVE_UNIX_SOCKETS
    if (family == AddressFamily::Unix) {
        if (address.empty()) return SocketError::BindFailed;
        if (address.size() >= sizeof(sockaddr_un::sun_path))
            return SocketError::BindFailed; // path too long
        sockaddr_un un{};
        un.sun_family = AF_UNIX;
        std::memcpy(un.sun_path, address.c_str(), address.size() + 1);
        std::memset(&out, 0, sizeof(out));
        std::memcpy(&out, &un, sizeof(un));
        outLen = static_cast<socklen_t>(
            offsetof(sockaddr_un, sun_path) + address.size() + 1);
        return SocketError::None;
    }
#endif // AISOCKS_HAVE_UNIX_SOCKETS
    if (family == AddressFamily::IPv6)
        return resolveIPv6_(
            address, port, sockType, doDns, out, outLen, gaiErr);
    return resolveIPv4_(address, port, sockType, doDns, out, outLen, gaiErr);
}

// -----------------------------------------------------------------------
// Error message formatting
// -----------------------------------------------------------------------

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
    // of template instantiation cost across every TU). Plain concatenation
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

// -----------------------------------------------------------------------
// Network interface enumeration (extracted from SocketImpl.cpp)
// -----------------------------------------------------------------------

#ifdef _WIN32
static std::vector<NetworkInterface> getLocalAddressesWindows_() {
    std::vector<NetworkInterface> interfaces;
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return interfaces;

    ULONG bufferSize = 15000;
    PIP_ADAPTER_ADDRESSES addresses = nullptr;
    for (;;) {
        addresses = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(malloc(bufferSize));
        if (!addresses) break;

        ULONG result = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX,
            nullptr, addresses, &bufferSize);
        if (result == ERROR_BUFFER_OVERFLOW) {
            ::free(addresses);
            addresses = nullptr;
            bufferSize *= 2;
            continue;
        }
        if (result == NO_ERROR) {
            for (PIP_ADAPTER_ADDRESSES adapter = addresses; adapter != nullptr;
                adapter = adapter->Next) {
                for (PIP_ADAPTER_UNICAST_ADDRESS unicast
                    = adapter->FirstUnicastAddress;
                    unicast != nullptr; unicast = unicast->Next) {
                    sockaddr* sa = unicast->Address.lpSockaddr;
                    NetworkInterface iface;
                    iface.name = std::string(adapter->AdapterName);
                    iface.isLoopback
                        = (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK);
                    if (sa->sa_family == AF_INET) {
                        char buf[INET_ADDRSTRLEN];
                        sockaddr_in* sin = reinterpret_cast<sockaddr_in*>(sa);
                        inet_ntop(
                            AF_INET, &sin->sin_addr, buf, INET_ADDRSTRLEN);
                        iface.address = buf;
                        iface.family = AddressFamily::IPv4;
                    } else if (sa->sa_family == AF_INET6) {
                        char buf[INET6_ADDRSTRLEN];
                        sockaddr_in6* sin6
                            = reinterpret_cast<sockaddr_in6*>(sa);
                        inet_ntop(
                            AF_INET6, &sin6->sin6_addr, buf, INET6_ADDRSTRLEN);
                        iface.address = buf;
                        iface.family = AddressFamily::IPv6;
                    } else {
                        continue;
                    }
                    interfaces.push_back(iface);
                }
            }
        }
        ::free(addresses);
        break;
    }
    return interfaces;
}
#else
static std::vector<NetworkInterface> getLocalAddressesUnix_() {
    std::vector<NetworkInterface> interfaces;
    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) != 0) return interfaces;

    for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) continue;
        const bool isLo = (ifa->ifa_flags & IFF_LOOPBACK) != 0;
        const int family = ifa->ifa_addr->sa_family;
        NetworkInterface iface;
        iface.name = ifa->ifa_name;
        iface.isLoopback = isLo;
        if (family == AF_INET) {
            char buf[INET_ADDRSTRLEN];
            sockaddr_in* sin = reinterpret_cast<sockaddr_in*>(ifa->ifa_addr);
            inet_ntop(AF_INET, &sin->sin_addr, buf, INET_ADDRSTRLEN);
            iface.address = buf;
            iface.family = AddressFamily::IPv4;
            interfaces.push_back(iface);
        } else if (family == AF_INET6) {
            char buf[INET6_ADDRSTRLEN];
            sockaddr_in6* sin6 = reinterpret_cast<sockaddr_in6*>(ifa->ifa_addr);
            inet_ntop(AF_INET6, &sin6->sin6_addr, buf, INET6_ADDRSTRLEN);
            iface.address = buf;
            iface.family = AddressFamily::IPv6;
            interfaces.push_back(iface);
        }
    }
    freeifaddrs(ifaddr);
    return interfaces;
}
#endif

std::vector<NetworkInterface> getLocalAddresses() {
#ifdef _WIN32
    return getLocalAddressesWindows_();
#else
    return getLocalAddressesUnix_();
#endif
}

// Specialized timeout setter (platform-specific logic)
bool setSocketOptionTimeout(SocketHandle socketHandle, int optname,
    std::chrono::milliseconds timeout, const char* errMsg) {
    (void)errMsg; // Suppress unused parameter warning
    const long long ms = timeout.count();
    int result = 0;
#ifdef _WIN32
    DWORD tv = static_cast<DWORD>(ms);
    result = setsockopt(socketHandle, SOL_SOCKET, optname,
        reinterpret_cast<const char*>(&tv), static_cast<socklen_t>(sizeof(tv)));
#else
    struct timeval tv;
    tv.tv_sec = static_cast<time_t>(ms / 1000);
    tv.tv_usec = static_cast<suseconds_t>((ms % 1000) * 1000);
    result = setsockopt(socketHandle, SOL_SOCKET, optname,
        reinterpret_cast<const char*>(&tv), static_cast<socklen_t>(sizeof(tv)));
#endif
    if (result != 0) {
#ifdef _WIN32
        const int errorCode = ::WSAGetLastError();
        if (errorCode == WSAEINVAL) return true;
#else
        const int errorCode = errno;
        if (errorCode == EINVAL) return true;
#endif
    }
    assert(result == 0
        && "setsockopt timeout failed - check socket handle and option "
           "parameters");
    return result == 0;
}

} // namespace aiSocks

// -----------------------------------------------------------------------
// IP address validation utilities
// -----------------------------------------------------------------------

namespace aiSocks {

bool isValidIPv4(const std::string& address) {
    struct sockaddr_in sa;
    return inet_pton(AF_INET, address.c_str(), &(sa.sin_addr)) == 1;
}

bool isValidIPv6(const std::string& address) {
    struct sockaddr_in6 sa;
    return inet_pton(AF_INET6, address.c_str(), &(sa.sin6_addr)) == 1;
}

} // namespace aiSocks
