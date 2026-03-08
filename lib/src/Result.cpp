// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it. PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#ifdef _WIN32
#include "pch.h"
#else
#include <netdb.h>
#endif
#include "Result.h"
#include <cstring>

namespace aiSocks {

// Builds a human-readable error message from the three fields captured at
// error-construction time.  Called at most once per Result — the caller caches
// the returned string in a unique_ptr<string> and never calls this again.
//
// Uses a stack buffer first (SSO-friendly) to avoid a heap round-trip for the
// common case where the full message fits in 128 bytes.
std::string buildErrorMessage(
    const char* description, int sysCode, bool isDns) {
    if (!description) return "Unknown error";

    // Small string optimization - use stack buffer for short messages
    static constexpr size_t SMALL_BUFFER_SIZE = 128;
    char stack_buffer[SMALL_BUFFER_SIZE];

    // Build message in stack buffer first
    int msg_len = snprintf(stack_buffer, SMALL_BUFFER_SIZE, "%s", description);

    if (sysCode != 0) {
        msg_len += snprintf(stack_buffer + msg_len,
            SMALL_BUFFER_SIZE - static_cast<size_t>(msg_len),
            " [%d: ", sysCode);

        // Platform-specific error string
        char sysErrBuf[256] = {0};
        if (isDns) {
            // gai_strerror is available on all platforms (POSIX + Windows)
            const char* gaiMsg = gai_strerror(sysCode);
            if (gaiMsg) {
#ifdef _WIN32
                strncpy_s(sysErrBuf, sizeof(sysErrBuf), gaiMsg,
                    sizeof(sysErrBuf) - 1);
#else
                strncpy(sysErrBuf, gaiMsg, sizeof(sysErrBuf) - 1);
                sysErrBuf[sizeof(sysErrBuf) - 1] = '\0';
#endif
            }
        } else {
#ifdef _WIN32
            FormatMessageA(
                FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr, sysCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                sysErrBuf, sizeof(sysErrBuf), nullptr);
#else
            const char* errnoMsg = strerror(sysCode);
            if (errnoMsg) {
                strncpy(sysErrBuf, errnoMsg, sizeof(sysErrBuf) - 1);
                sysErrBuf[sizeof(sysErrBuf) - 1] = '\0';
            }
#endif
        }
        // Trim trailing whitespace/newlines
        char* end = sysErrBuf + strlen(sysErrBuf) - 1;
        while (
            end >= sysErrBuf && (*end == '\n' || *end == '\r' || *end == ' ')) {
            *end = '\0';
            --end;
        }

        msg_len += snprintf(stack_buffer + msg_len,
            SMALL_BUFFER_SIZE - static_cast<size_t>(msg_len), "%s]", sysErrBuf);
    }

    // Return the message — fits in the stack buffer for almost all real errors.
    return std::string(stack_buffer, static_cast<size_t>(msg_len));
}

} // namespace aiSocks
