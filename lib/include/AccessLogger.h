// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#pragma once

// ---------------------------------------------------------------------------
// AccessLogger — Apache Combined Log Format HTTP access logging.
//
// Output format (one line per completed response):
//   %h - %u [%d/%b/%Y:%H:%M:%S +0000] "%r" %s %b
//
// Example:
//   192.0.2.1 - alice [13/Mar/2026:12:34:56 +0000] "GET /foo HTTP/1.1" 200 512
//
// Field mapping:
//   %h  remote IP address
//   -   RFC 1413 ident (always '-')
//   %u  authenticated user (or '-' if none)
//   %t  timestamp in Apache combined format (UTC)
//   %r  first line of the request
//   %s  HTTP status code
//   %b  response size in bytes (headers + body)
//
// Thread-safety: NOT thread-safe.  Must be called from the poll-loop thread,
// which matches every HttpPollServer usage pattern.
// ---------------------------------------------------------------------------

#include "FileIO.h"

#include <cstddef>
#include <ctime>
#include <string>
#include <string_view>

namespace aiSocks {

class AccessLogger {
    public:
    AccessLogger() = default;
    explicit AccessLogger(const std::string& path);

    // Open (or reopen) the log file in append mode.
    // Returns true on success.
    bool open(const std::string& path);
    void close();
    bool isOpen() const { return file_.isOpen(); }

    // Append one Combined Log entry.
    // peerIp        remote address (e.g. "192.0.2.1")
    // requestLine   "METHOD /path HTTP/x.y"   (pass "-" if unavailable)
    // statusCode    HTTP status code (e.g. 200, 404)
    // responseBytes total bytes in the response (headers + body)
    // user          authenticated username, or empty/omitted for "-"
    void log(const std::string& peerIp, const std::string& requestLine,
        int statusCode, size_t responseBytes, const std::string& user = {});

    // Flush any buffered log entries to disk.  Called automatically every
    // 64 writes; also call explicitly on idle or shutdown to ensure no
    // entries are lost.
    void flush();

    // ---- static helpers used by the wiring code -------------------------

    // Extract the first request line from a raw HTTP request buffer.
    // Returns "-" when the buffer is empty or cannot be parsed.
    static std::string extractRequestLine(const std::string& requestBuf);

    // Extract the HTTP status code from the first line of a raw response.
    // Returns 0 when the buffer is empty or unparseable.
    static int extractStatusCode(std::string_view responseBuf);

    private:
    File file_;
    unsigned writeCount_{0};

    // Format `t` as "13/Mar/2026:12:34:56 +0000" (UTC, Apache style).
    static std::string formatTimestamp(std::time_t t);
};

} // namespace aiSocks
