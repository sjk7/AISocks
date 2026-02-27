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

