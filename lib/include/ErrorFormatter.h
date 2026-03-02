// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
#ifndef AISOCKS_ERROR_FORMATTER_H
#define AISOCKS_ERROR_FORMATTER_H

#include "Socket.h"
#include "SocketImplHelpers.h"
#include <string>

namespace aiSocks {

// ---------------------------------------------------------------------------
// ErrorFormatter - Formats error messages consistently across the library.
// ---------------------------------------------------------------------------

// Format error message with step and OS description.
// Format: "step: description [sysCode: systemMessage]"
inline std::string formatErrorMessage(
    const std::string& step, const ErrorContext& ctx) {
    std::string result = step + ": " + formatErrorContext(ctx);
    return result;
}

// Format error message from SocketImpl for compatibility
inline std::string formatErrorMessage(
    const std::string& step, const Socket& socket) {
    if (!socket.isValid()) {
        auto msg = socket.getErrorMessage();
        return step + ": " + msg;
    }
    return "";
}

} // namespace aiSocks

#endif // AISOCKS_ERROR_FORMATTER_H
