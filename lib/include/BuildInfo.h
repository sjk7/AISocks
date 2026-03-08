// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it. PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#ifndef AISOCKS_BUILD_INFO_H
#define AISOCKS_BUILD_INFO_H

#include <cstdio>

namespace aiSocks {
namespace BuildInfo {

    // ---------------------------------------------------------------------------
    // Compile-time platform and build-type queries.
    // All functions are inline constexpr / inline — zero runtime overhead.
    // ---------------------------------------------------------------------------

    inline constexpr const char* os() noexcept {
#if defined(__APPLE__)
        return "macOS";
#elif defined(__linux__)
        return "Linux";
#elif defined(_WIN32)
        return "Windows";
#else
        return "Unknown";
#endif
    }

    inline constexpr const char* kind() noexcept {
#if defined(NDEBUG)
        return "Release";
#else
        return "Debug";
#endif
    }

    // Prints a one-line build summary to stdout.
    inline void print() {
        printf("Built: %s %s  |  OS: %s  |  Build: %s\n", __DATE__, __TIME__,
            os(), kind());
    }

} // namespace BuildInfo
} // namespace aiSocks

#endif // AISOCKS_BUILD_INFO_H
