// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
#pragma once
#include <iostream>
#include <string>
#include <cstdint>
#include <stdexcept>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#endif

// Simple test framework for aiSocks tests.
// Each test binary returns 0 on full pass, 1 on any failure.

static int g_failed = 0;
static int g_passed = 0;

#define REQUIRE(expr)                                                      \
    do {                                                                   \
        if (!(expr)) {                                                     \
            std::cerr << "  FAIL [" << __FILE__ << ":" << __LINE__ << "] " \
                      << #expr << "\n";                                    \
            ++g_failed;                                                    \
        } else {                                                           \
            std::cout << "  pass: " << #expr << "\n";                      \
            ++g_passed;                                                    \
        }                                                                  \
    } while (0)

#define REQUIRE_MSG(expr, msg)                                             \
    do {                                                                   \
        if (!(expr)) {                                                     \
            std::cerr << "  FAIL [" << __FILE__ << ":" << __LINE__ << "] " \
                      << msg << "\n";                                      \
            ++g_failed;                                                    \
        } else {                                                           \
            std::cout << "  pass: " << msg << "\n";                        \
            ++g_passed;                                                    \
        }                                                                  \
    } while (0)

#define BEGIN_TEST(name)                           \
    do {                                           \
        std::cout << "\n--- " << name << " ---\n"; \
    } while (0)

inline int test_summary() {
    std::cout << "\n==============================\n";
    std::cout << "Results: " << g_passed << " passed, " << g_failed
              << " failed\n";
    return (g_failed > 0) ? 1 : 0;
}

// Ask the OS for a free ephemeral port by binding to port 0, reading the
// assigned port via getsockname(), then closing the socket.
//
// There is an inherent TOCTOU window between closing the helper socket and
// the test binding its own socket to the same port, but in practice the
// OS will not immediately recycle the port, making this safe for testing.
//
// Returns the port number (> 0) on success, or 0 if the OS call fails.
inline uint16_t pickFreePort() {
#ifdef _WIN32
    // Ensure WinSock is up (may already be initialised by the library).
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == INVALID_SOCKET) return 0;
    sockaddr_in sa{};
    sa.sin_family      = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sa.sin_port        = 0;
    if (::bind(fd, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) != 0) {
        ::closesocket(fd);
        return 0;
    }
    int len = static_cast<int>(sizeof(sa));
    ::getsockname(fd, reinterpret_cast<sockaddr*>(&sa), &len);
    ::closesocket(fd);
#else
    int fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == -1) return 0;
    sockaddr_in sa{};
    sa.sin_family      = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sa.sin_port        = 0;
    if (::bind(fd, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) != 0) {
        ::close(fd);
        return 0;
    }
    socklen_t len = static_cast<socklen_t>(sizeof(sa));
    ::getsockname(fd, reinterpret_cast<sockaddr*>(&sa), &len);
    ::close(fd);
#endif
    return ntohs(sa.sin_port);
}
