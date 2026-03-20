// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Tests for AccessLogger: Apache Combined Log Format output, static parsing
// helpers, and file I/O behaviour.

#include "AccessLogger.h"
#include "FileIO.h"
#include "test_helpers.h"

#include <cstdio>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#define unlink(p) DeleteFileA(p)
#else
#include <unistd.h>
#endif

using namespace aiSocks;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static const char* kLogPath = "test_access_logger_tmp.log";

static void removeLog() {
    unlink(kLogPath);
}

// Read the entire log file into a string.
static std::string readLog() {
    File f(kLogPath, "r");
    if (!f.isOpen()) return {};
    auto bytes = f.readAll();
    return std::string(bytes.begin(), bytes.end());
}

// Split a string by '\n', omitting empty trailing element.
static std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> lines;
    size_t pos = 0;
    while (pos <= s.size()) {
        const size_t nl = s.find('\n', pos);
        const size_t end = (nl == std::string::npos) ? s.size() : nl;
        if (end > pos || nl != std::string::npos)
            lines.emplace_back(s.substr(pos, end - pos));
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
    // Drop trailing empty element if present
    while (!lines.empty() && lines.back().empty()) lines.pop_back();
    return lines;
}

// ---------------------------------------------------------------------------
// static helper tests — no file I/O
// ---------------------------------------------------------------------------

int main() {
    printf("=== AccessLogger Tests ===\n");

    // ------------------------------------------------------------------
    BEGIN_TEST("extractRequestLine: normal GET request");
    {
        const std::string req = "GET /index.html HTTP/1.1\r\n"
                                "Host: example.com\r\n\r\n";
        REQUIRE(AccessLogger::extractRequestLine(req)
            == "GET /index.html HTTP/1.1");
    }

    // ------------------------------------------------------------------
    BEGIN_TEST("extractRequestLine: LF-only line ending");
    {
        const std::string req = "GET /foo HTTP/1.0\nHost: x\n\n";
        REQUIRE(AccessLogger::extractRequestLine(req) == "GET /foo HTTP/1.0");
    }

    // ------------------------------------------------------------------
    BEGIN_TEST("extractRequestLine: empty buffer returns '-'");
    {
        REQUIRE(AccessLogger::extractRequestLine("") == "-");
    }

    // ------------------------------------------------------------------
    BEGIN_TEST("extractStatusCode: 200 OK");
    {
        const std::string resp = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
        REQUIRE(AccessLogger::extractStatusCode(resp) == 200);
    }

    // ------------------------------------------------------------------
    BEGIN_TEST("extractStatusCode: 404 Not Found");
    {
        const std::string resp = "HTTP/1.1 404 Not Found\r\n\r\n";
        REQUIRE(AccessLogger::extractStatusCode(resp) == 404);
    }

    // ------------------------------------------------------------------
    BEGIN_TEST("extractStatusCode: 403 Forbidden");
    {
        const std::string resp = "HTTP/1.1 403 Forbidden\r\n\r\n";
        REQUIRE(AccessLogger::extractStatusCode(resp) == 403);
    }

    // ------------------------------------------------------------------
    BEGIN_TEST("extractStatusCode: 301 Redirect");
    {
        const std::string resp = "HTTP/1.1 301 Moved Permanently\r\n\r\n";
        REQUIRE(AccessLogger::extractStatusCode(resp) == 301);
    }

    // ------------------------------------------------------------------
    BEGIN_TEST("extractStatusCode: empty buffer returns 0");
    {
        REQUIRE(AccessLogger::extractStatusCode("") == 0);
    }

    // ------------------------------------------------------------------
    BEGIN_TEST("extractStatusCode: malformed first line returns 0");
    {
        REQUIRE(AccessLogger::extractStatusCode("GARBAGE") == 0);
        REQUIRE(AccessLogger::extractStatusCode("HTTP/1.1 abc OK") == 0);
    }

    // ------------------------------------------------------------------
    // File I/O tests
    // ------------------------------------------------------------------

    // ------------------------------------------------------------------
    BEGIN_TEST("open: creates file; isOpen returns true");
    {
        removeLog();
        AccessLogger logger;
        REQUIRE(!logger.isOpen());
        REQUIRE(logger.open(kLogPath));
        REQUIRE(logger.isOpen());
        logger.close();
    }

    // ------------------------------------------------------------------
    BEGIN_TEST("log: single entry written in Combined Log Format");
    {
        removeLog();
        AccessLogger logger(kLogPath);
        REQUIRE(logger.isOpen());

        logger.log("192.0.2.1", "GET /index.html HTTP/1.1", 200, 512);
        logger.close();

        const std::string content = readLog();
        REQUIRE(!content.empty());

        // Must start with the peer IP
        REQUIRE(content.substr(0, 9) == "192.0.2.1");

        // Must contain the request line in quotes
        REQUIRE(
            content.find("\"GET /index.html HTTP/1.1\"") != std::string::npos);

        // Must contain the status code
        REQUIRE(content.find(" 200 ") != std::string::npos);

        // Must contain the byte count
        REQUIRE(content.find("512") != std::string::npos);

        // Must contain a timestamp bracket [
        REQUIRE(content.find('[') != std::string::npos);

        // Must end with a newline
        REQUIRE(!content.empty() && content.back() == '\n');
    }

    // ------------------------------------------------------------------
    BEGIN_TEST("log: authenticated user name replaces '-'");
    {
        removeLog();
        AccessLogger logger(kLogPath);
        logger.log("10.0.0.1", "GET /private HTTP/1.1", 200, 100, "alice");
        logger.close();

        const std::string content = readLog();
        REQUIRE(content.find(" alice ") != std::string::npos);
    }

    // ------------------------------------------------------------------
    BEGIN_TEST("log: empty user writes '-'");
    {
        removeLog();
        AccessLogger logger(kLogPath);
        logger.log("10.0.0.1", "GET /pub HTTP/1.1", 200, 100, "");
        logger.close();

        const std::string content = readLog();
        // Format: "<ip> - - [...]..."
        REQUIRE(content.find("- -") != std::string::npos);
    }

    // ------------------------------------------------------------------
    BEGIN_TEST("log: multiple entries written as separate lines");
    {
        removeLog();
        AccessLogger logger(kLogPath);
        logger.log("1.1.1.1", "GET /a HTTP/1.1", 200, 10);
        logger.log("2.2.2.2", "GET /b HTTP/1.1", 404, 20);
        logger.log("3.3.3.3", "POST /c HTTP/1.1", 403, 30);
        logger.close();

        const auto lines = splitLines(readLog());
        REQUIRE(lines.size() == 3);
        REQUIRE(lines[0].substr(0, 7) == "1.1.1.1");
        REQUIRE(lines[1].substr(0, 7) == "2.2.2.2");
        REQUIRE(lines[2].substr(0, 7) == "3.3.3.3");
        REQUIRE(lines[1].find("404") != std::string::npos);
        REQUIRE(lines[2].find("403") != std::string::npos);
    }

    // ------------------------------------------------------------------
    BEGIN_TEST("log: appends to existing file across open/close cycles");
    {
        removeLog();
        {
            AccessLogger logger(kLogPath);
            logger.log("1.1.1.1", "GET /first HTTP/1.1", 200, 1);
        }
        {
            AccessLogger logger(kLogPath);
            logger.log("2.2.2.2", "GET /second HTTP/1.1", 200, 2);
        }

        const auto lines = splitLines(readLog());
        REQUIRE(lines.size() == 2);
        REQUIRE(lines[0].find("/first") != std::string::npos);
        REQUIRE(lines[1].find("/second") != std::string::npos);
    }

    // ------------------------------------------------------------------
    BEGIN_TEST("log: no-op when logger is closed");
    {
        removeLog();
        AccessLogger logger; // not opened
        logger.log("9.9.9.9", "GET /x HTTP/1.1", 200, 0); // must not crash
        REQUIRE(!logger.isOpen());
        // File should not have been created
        File f(kLogPath, "r");
        REQUIRE(!f.isOpen());
    }

    // ------------------------------------------------------------------
    BEGIN_TEST("timestamp format: contains Apache-style date separators");
    {
        removeLog();
        AccessLogger logger(kLogPath);
        logger.log("127.0.0.1", "GET / HTTP/1.1", 200, 0);
        logger.close();

        const std::string content = readLog();
        // Timestamp field is bracketed: [DD/Mon/YYYY:HH:MM:SS +0000]
        const size_t ob = content.find('[');
        const size_t cb = content.find(']');
        REQUIRE(ob != std::string::npos);
        REQUIRE(cb != std::string::npos);
        REQUIRE(cb > ob);

        const std::string ts = content.substr(ob + 1, cb - ob - 1);
        // Should contain '/' as date separators and ':' as time separators
        REQUIRE(ts.find('/') != std::string::npos);
        REQUIRE(ts.find(':') != std::string::npos);
        // Should end with "+0000" (UTC marker)
        REQUIRE(ts.size() > 5 && ts.substr(ts.size() - 5) == "+0000");
    }

    removeLog();

    return test_summary();
}
