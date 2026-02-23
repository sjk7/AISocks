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
// ErrorFormatter - Provides exact same error string format as SocketException
// for test compatibility while maintaining exception-free design
// ---------------------------------------------------------------------------

// Format error message in exact same format as SocketException::what()
// Format: "step: description [sysCode: systemMessage]"
inline std::string formatErrorMessage(const std::string& step, const ErrorContext& ctx) {
    std::string result = step + ": " + formatErrorContext(ctx);
    return result;
}

// Format error message from SocketImpl for compatibility
inline std::string formatErrorMessage(const std::string& step, const Socket& socket) {
    if (!socket.isValid()) {
        auto err = socket.getLastError();
        auto msg = socket.getErrorMessage();
        return step + ": " + msg;
    }
    return "";
}

// Get error context from SocketImpl for formatting
inline ErrorContext getErrorContext(const Socket& socket) {
    // This would need access to SocketImpl, but for now we'll create
    // a basic context from the public interface
    ErrorContext ctx{};
    ctx.description = "Unknown error";
    ctx.sysCode = 0;
    ctx.isDns = false;
    
    // Try to extract more detailed error information
    auto err = socket.getLastError();
    auto msg = socket.getErrorMessage();
    
    // Parse the message to extract description and system code
    // Message format: "description [sysCode: systemMessage]"
    auto bracket_pos = msg.find('[');
    if (bracket_pos != std::string::npos) {
        ctx.description = msg.substr(0, bracket_pos - 1); // Remove trailing space
        
        auto colon_pos = msg.find(':', bracket_pos);
        if (colon_pos != std::string::npos) {
            try {
                std::string code_str = msg.substr(bracket_pos + 1, colon_pos - bracket_pos - 1);
                ctx.sysCode = std::stoi(code_str);
            } catch (...) {
                ctx.sysCode = 0;
            }
        }
    } else {
        ctx.description = msg.c_str();
    }
    
    return ctx;
}

} // namespace aiSocks

#endif // AISOCKS_ERROR_FORMATTER_H
