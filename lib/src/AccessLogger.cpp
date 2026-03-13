// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#include "AccessLogger.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace aiSocks {

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

AccessLogger::AccessLogger(const std::string& path) {
    open(path);
}

// ---------------------------------------------------------------------------
// open / close
// ---------------------------------------------------------------------------

bool AccessLogger::open(const std::string& path) {
    file_.close();
    return file_.open(path.c_str(), "a");
}

void AccessLogger::close() {
    file_.close();
}

// ---------------------------------------------------------------------------
// log
// ---------------------------------------------------------------------------

void AccessLogger::log(const std::string& peerIp,
    const std::string& requestLine, int statusCode, size_t responseBytes,
    const std::string& user) {
    if (!file_.isOpen()) return;

    const std::string ts = formatTimestamp(std::time(nullptr));
    const char* u = user.empty() ? "-" : user.c_str();
    const std::string rl = requestLine.empty() ? "-" : requestLine;

    // Build the line in a small character buffer where possible to avoid
    // excessive allocations.  The status + size suffix fits in 32 bytes.
    char suffix[48];
    snprintf(suffix, sizeof(suffix), "\" %d %zu\n", statusCode, responseBytes);

    file_.writeString(peerIp);
    file_.writeString(" - ");
    file_.writeString(u);
    file_.writeString(" [");
    file_.writeString(ts);
    file_.writeString("] \"");
    file_.writeString(rl);
    file_.writeString(suffix);
    file_.flush();
}

// ---------------------------------------------------------------------------
// extractRequestLine
// ---------------------------------------------------------------------------

std::string AccessLogger::extractRequestLine(const std::string& requestBuf) {
    if (requestBuf.empty()) return "-";
    // First line ends at '\r' or '\n', whichever comes first.
    const size_t cr = requestBuf.find('\r');
    const size_t lf = requestBuf.find('\n');
    size_t end = std::string::npos;
    if (cr != std::string::npos && lf != std::string::npos)
        end = (cr < lf) ? cr : lf;
    else if (cr != std::string::npos)
        end = cr;
    else if (lf != std::string::npos)
        end = lf;

    if (end == std::string::npos) return requestBuf; // single-line buffer
    if (end == 0) return "-";
    return requestBuf.substr(0, end);
}

// ---------------------------------------------------------------------------
// extractStatusCode
// ---------------------------------------------------------------------------

int AccessLogger::extractStatusCode(std::string_view responseBuf) {
    // Expected: "HTTP/1.x NNN ..."
    const size_t sp1 = responseBuf.find(' ');
    if (sp1 == std::string::npos || sp1 + 1 >= responseBuf.size()) return 0;
    const char* p = responseBuf.data() + sp1 + 1;
    char* end = nullptr;
    const long code = std::strtol(p, &end, 10);
    if (end == p || code < 100 || code > 999) return 0;
    return static_cast<int>(code);
}

// ---------------------------------------------------------------------------
// formatTimestamp
// ---------------------------------------------------------------------------

std::string AccessLogger::formatTimestamp(std::time_t t) {
    char buf[40];
    buf[0] = '\0';
#ifdef _WIN32
    struct tm tm_info{};
    gmtime_s(&tm_info, &t);
    strftime(buf, sizeof(buf), "%d/%b/%Y:%H:%M:%S +0000", &tm_info);
#else
    struct tm tm_info{};
    gmtime_r(&t, &tm_info);
    strftime(buf, sizeof(buf), "%d/%b/%Y:%H:%M:%S +0000", &tm_info);
#endif
    return buf;
}

} // namespace aiSocks
