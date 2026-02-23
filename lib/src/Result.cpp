// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
#ifdef _WIN32
#include "pch.h"
#endif
#include "Result.h"
#include <cstring>

namespace aiSocks {

// Platform-specific error message implementation for ErrorInfo
std::string ErrorInfo::buildMessage() const {
    if (!description) return "Unknown error";
    
    std::string msg = description;
    if (sysCode != 0) {
        msg += " [";
        msg += std::to_string(sysCode);
        msg += ": ";
        
        // Platform-specific error string
        char sysErrBuf[256] = {0};
#ifdef _WIN32
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                     nullptr, sysCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                     sysErrBuf, sizeof(sysErrBuf), nullptr);
#else
        if (isDns) {
            // DNS errors use gai_strerror
            const char* gaiMsg = gai_strerror(sysCode);
            if (gaiMsg) {
                strncpy(sysErrBuf, gaiMsg, sizeof(sysErrBuf) - 1);
            }
        } else {
            // Regular errno errors
            const char* errnoMsg = strerror(sysCode);
            if (errnoMsg) {
                strncpy(sysErrBuf, errnoMsg, sizeof(sysErrBuf) - 1);
            }
        }
#endif
        // Trim trailing whitespace/newlines
        char* end = sysErrBuf + strlen(sysErrBuf) - 1;
        while (end >= sysErrBuf && (*end == '\n' || *end == '\r' || *end == ' ')) {
            *end = '\0';
            --end;
        }
        
        msg += sysErrBuf;
        msg += "]";
    }
    return msg;
}

} // namespace aiSocks
