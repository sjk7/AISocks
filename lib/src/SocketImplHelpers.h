// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#ifndef AISOCKS_SOCKET_IMPL_HELPERS_H
#define AISOCKS_SOCKET_IMPL_HELPERS_H

#include "Socket.h"
#include <string>
#include <cstddef>
#include <cassert>

// Forward declaration for SocketHandle
#ifdef _WIN32
using SocketHandle = SOCKET;
#else
using SocketHandle = int;
#endif

// Forward declarations for setsockopt
#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#endif

namespace aiSocks {

// Forward declarations
struct ErrorContext;

// Error classification helper
SocketError classifyTransferSysError(int sysErr) noexcept;

// Address resolution helper
SocketError resolveToSockaddr(const std::string& address, Port port,
    AddressFamily family, SocketType sockType, bool doDns,
    sockaddr_storage& out, socklen_t& outLen, int* gaiErr = nullptr);

// Error message formatting
std::string formatErrorContext(const ErrorContext& ctx);

// Network interface enumeration
std::vector<NetworkInterface> getLocalAddresses();

// Generic socket option setter template
template <typename T>
bool setSocketOption(SocketHandle socketHandle, int level, int optname,
    const T& value, const char* errMsg) {
    int result = setsockopt(socketHandle, level, optname,
        reinterpret_cast<const char*>(&value),
        static_cast<socklen_t>(sizeof(value)));

    if (result != 0) {
#ifdef _WIN32
        int errorCode = WSAGetLastError();
#else
        int errorCode = errno;
#endif
#ifdef _WIN32
        LPSTR errorText = nullptr;
        FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER
                | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, errno, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPSTR)&errorText, 0, nullptr);
        if (errorText) {
            fprintf(stderr, "errno: %d : %s\n", errno, errorText);
            LocalFree(errorText);
        } else {
            fprintf(
                stderr, "errno: %d : (unable to get error message)\n", errno);
        }
#else
        fprintf(stderr, "errno: %d : %s\n", errno, strerror(errno));
#endif
        fprintf(stderr, "setsockopt FAILED: %s\n", errMsg);
        fprintf(
            stderr, "  Socket handle: %d\n", static_cast<int>(socketHandle));
        fprintf(stderr, "  Level: %d, Optname: %d\n", level, optname);
        fprintf(stderr, "  Error code: %d\n", errorCode);

#ifdef _WIN32
        LPSTR errorCodeText = nullptr;
        FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER
                | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPSTR)&errorCodeText, 0, nullptr);
        if (errorCodeText) {
            fprintf(stderr, "  Error message: %s", errorCodeText);
            LocalFree(errorCodeText);
        }
#else
        fprintf(stderr, "  Error message: %s\n", strerror(errorCode));
#endif

    }

    return result == 0;
}

// Specialized timeout setter (platform-specific logic)
bool setSocketOptionTimeout(SocketHandle socketHandle, int optname,
    std::chrono::milliseconds timeout, const char* errMsg);

// IP address validation utilities
bool isValidIPv4(const std::string& address);
bool isValidIPv6(const std::string& address);

// Validation helper macro for public-facing functions
#define RETURN_IF_INVALID()                                                    \
    do {                                                                       \
        if (!isValid()) {                                                      \
            setError(SocketError::InvalidSocket, "Socket is not valid");       \
            return false;                                                      \
        }                                                                      \
    } while (0)

#define SET_SUCCESS()                                                          \
    do {                                                                       \
        lastError = SocketError::None;                                         \
    } while (0)

} // namespace aiSocks

#endif // AISOCKS_SOCKET_IMPL_HELPERS_H
