// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#pragma once
#include <cstdio>
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

#define REQUIRE(expr)                                                          \
    do {                                                                       \
        if (!(expr)) {                                                         \
            fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__, #expr); \
            ++g_failed;                                                        \
        } else {                                                               \
            printf("  pass: %s\n", #expr);                                     \
            ++g_passed;                                                        \
        }                                                                      \
    } while (0)

#define REQUIRE_MSG(expr, msg)                                                 \
    do {                                                                       \
        if (!(expr)) {                                                         \
            fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__,        \
                    std::string(msg).c_str());                                 \
            ++g_failed;                                                        \
        } else {                                                               \
            printf("  pass: %s\n", std::string(msg).c_str());                 \
            ++g_passed;                                                        \
        }                                                                      \
    } while (0)

#define BEGIN_TEST(name)                                                       \
    do {                                                                       \
        printf("\n--- %s ---\n", name);                                        \
    } while (0)

inline int test_summary() {
    printf("\n==============================\n");
    printf("Results: %d passed, %d failed\n", g_passed, g_failed);
    return (g_failed > 0) ? 1 : 0;
}
