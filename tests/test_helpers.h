// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#pragma once
#include <atomic>
#include <cstdio>
#include <string>
#include <cstdint>
#include <stdexcept>
#include "Stopwatch.h"

// Simple test framework for aiSocks tests.
// Each test binary returns 0 on full pass, 1 on any failure.

static std::atomic<int> g_failed{0};
static std::atomic<int> g_passed{0};
static aiSocks::Stopwatch g_totalTimer;
static aiSocks::Stopwatch g_testTimer;
static std::string g_currentTest;

#ifdef _MSC_VER
#define REQUIRE(expr)                                                          \
    __pragma(warning(push))                                                    \
    __pragma(warning(disable : 4127))                                          \
    do {                                                                       \
        if (!(expr)) {                                                         \
            fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__, #expr); \
            ++g_failed;                                                        \
        } else {                                                               \
            printf("  pass: %s\n", #expr);                                     \
            ++g_passed;                                                        \
        }                                                                      \
    } while (0)                                                                \
    __pragma(warning(pop))
#else
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
#endif

#define REQUIRE_MSG(expr, msg)                                                 \
    do {                                                                       \
        if (!(expr)) {                                                         \
            fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__,         \
                std::string(msg).c_str());                                     \
            ++g_failed;                                                        \
        } else {                                                               \
            printf("  pass: %s\n", std::string(msg).c_str());                  \
            ++g_passed;                                                        \
        }                                                                      \
    } while (0)

#define BEGIN_TEST(name)                                                       \
    do {                                                                       \
        if (!g_currentTest.empty()) {                                          \
            printf("  [%.1f ms]\n", g_testTimer.elapsedMs());                  \
            fflush(stdout);                                                    \
        }                                                                      \
        g_currentTest = (name);                                                \
        g_testTimer.reset();                                                   \
        printf("\n--- %s ---\n", name);                                        \
        fflush(stdout);                                                        \
    } while (0)

inline int test_summary() {
    if (!g_currentTest.empty()) {
        printf("  [%.1f ms]\n", g_testTimer.elapsedMs());
    }
    printf("\n==============================\n");
    printf("Results: %d passed, %d failed\n", g_passed.load(), g_failed.load());
    printf("Total time: %.1f ms\n", g_totalTimer.elapsedMs());
    return (g_failed > 0) ? 1 : 0;
}
