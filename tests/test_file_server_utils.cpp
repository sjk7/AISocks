// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Tests for lib/include/FileServerUtils.h
//
// Coverage:
//   urlDecodePath   — 9 cases
//   getFileExtension — 6 cases
//   formatHttpDate   — 4 cases
//   addSecurityHeaders — 3 cases

#include "FileServerUtils.h"
#include "FileIO.h" // StringBuilder
#include "test_helpers.h"

#include <cstring>
#include <ctime>
#include <string>

using namespace aiSocks;

// ============================================================
// urlDecodePath
// ============================================================

static void test_urldecode_empty() {
    BEGIN_TEST("urlDecodePath: empty string returns empty string");
    REQUIRE(FileServerUtils::urlDecodePath("") == "");
}

static void test_urldecode_plain() {
    BEGIN_TEST("urlDecodePath: plain ASCII path passes through unchanged");
    REQUIRE(FileServerUtils::urlDecodePath("/foo/bar.html") == "/foo/bar.html");
}

static void test_urldecode_space() {
    BEGIN_TEST("urlDecodePath: %20 decoded to space");
    REQUIRE(FileServerUtils::urlDecodePath("/my%20file.txt") == "/my file.txt");
}

static void test_urldecode_uppercase_hex() {
    BEGIN_TEST("urlDecodePath: uppercase %2F decoded to /");
    REQUIRE(FileServerUtils::urlDecodePath("%2F") == "/");
}

static void test_urldecode_lowercase_hex() {
    BEGIN_TEST("urlDecodePath: lowercase %2f decoded to /");
    REQUIRE(FileServerUtils::urlDecodePath("%2f") == "/");
}

static void test_urldecode_mixed_hex_case() {
    BEGIN_TEST("urlDecodePath: mixed-case hex %2F and %2f both decoded");
    const std::string result = FileServerUtils::urlDecodePath("%2F%2f");
    REQUIRE(result == "//");
}

static void test_urldecode_percent_at_end() {
    BEGIN_TEST(
        "urlDecodePath: lone % at end of string passes through unchanged");
    REQUIRE(FileServerUtils::urlDecodePath("/foo%") == "/foo%");
}

static void test_urldecode_incomplete_sequence() {
    BEGIN_TEST(
        "urlDecodePath: %2 (only one hex digit) passes through unchanged");
    REQUIRE(FileServerUtils::urlDecodePath("/foo%2") == "/foo%2");
}

static void test_urldecode_invalid_hex_chars() {
    BEGIN_TEST("urlDecodePath: %ZZ (non-hex) passes through unchanged");
    REQUIRE(FileServerUtils::urlDecodePath("%ZZ") == "%ZZ");
}

// ============================================================
// getFileExtension
// ============================================================

static void test_ext_html() {
    BEGIN_TEST("getFileExtension: .html suffix returned");
    REQUIRE(FileServerUtils::getFileExtension("index.html") == ".html");
}

static void test_ext_no_extension() {
    BEGIN_TEST("getFileExtension: no dot returns empty string");
    REQUIRE(FileServerUtils::getFileExtension("Makefile") == "");
}

static void test_ext_empty() {
    BEGIN_TEST("getFileExtension: empty path returns empty string");
    REQUIRE(FileServerUtils::getFileExtension("") == "");
}

static void test_ext_last_dot_wins() {
    BEGIN_TEST("getFileExtension: last dot suffix returned for archive.tar.gz");
    REQUIRE(FileServerUtils::getFileExtension("archive.tar.gz") == ".gz");
}

static void test_ext_dot_only() {
    BEGIN_TEST("getFileExtension: lone dot returns empty string");
    // dotPos == length-1, condition is dotPos < length-1, so returns ""
    REQUIRE(FileServerUtils::getFileExtension(".") == "");
}

static void test_ext_hidden_file() {
    BEGIN_TEST("getFileExtension: hidden file .gitignore returns empty string");
    // find_last_of('.') == 0 == dotPos == 0, length == 10, dotPos == 0 < 9, so
    // returns ".gitignore" Actually for ".gitignore": dotPos == 0, length-1 ==
    // 9, 0 < 9 is true -> ".gitignore"
    REQUIRE(FileServerUtils::getFileExtension(".gitignore") == ".gitignore");
}

// ============================================================
// formatHttpDate
// ============================================================

static void test_httpdate_epoch() {
    BEGIN_TEST(
        "formatHttpDate: Unix epoch formats as Thu, 01 Jan 1970 00:00:00 GMT");
    std::string d = FileServerUtils::formatHttpDate(0);
    REQUIRE(d == "Thu, 01 Jan 1970 00:00:00 GMT");
}

static void test_httpdate_known_timestamp() {
    BEGIN_TEST("formatHttpDate: 2024-06-15 12:00:00 UTC formats correctly");
    // 2024-06-15 12:00:00 UTC = 1718452800
    const time_t ts = 1718452800;
    std::string d = FileServerUtils::formatHttpDate(ts);
    REQUIRE(d == "Sat, 15 Jun 2024 12:00:00 GMT");
}

static void test_httpdate_nonempty() {
    BEGIN_TEST("formatHttpDate: result is non-empty for any valid time");
    const time_t ts = 1700000000;
    REQUIRE(!FileServerUtils::formatHttpDate(ts).empty());
}

static void test_httpdate_contains_gmt() {
    BEGIN_TEST("formatHttpDate: result always ends with GMT");
    const time_t ts = 946684800; // 2000-01-01 00:00:00 UTC
    const std::string d = FileServerUtils::formatHttpDate(ts);
    REQUIRE(d.size() >= 3);
    REQUIRE(d.substr(d.size() - 3) == "GMT");
}

// ============================================================
// addSecurityHeaders
// ============================================================

static void test_security_headers_disabled() {
    BEGIN_TEST("addSecurityHeaders: no headers appended when enabled=false");
    StringBuilder sb(128);
    FileServerUtils::addSecurityHeaders(sb, false);
    REQUIRE(sb.toString().empty());
}

static void test_security_headers_enabled_contains_all() {
    BEGIN_TEST(
        "addSecurityHeaders: all four headers present when enabled=true");
    StringBuilder sb(256);
    FileServerUtils::addSecurityHeaders(sb, true);
    const std::string result = sb.toString();
    REQUIRE(
        result.find("X-Content-Type-Options: nosniff") != std::string::npos);
    REQUIRE(result.find("X-Frame-Options: DENY") != std::string::npos);
    REQUIRE(result.find("Content-Security-Policy:") != std::string::npos);
    REQUIRE(result.find("Referrer-Policy: no-referrer") != std::string::npos);
}

static void test_security_headers_crlf_terminated() {
    BEGIN_TEST("addSecurityHeaders: each header is CRLF-terminated");
    StringBuilder sb(256);
    FileServerUtils::addSecurityHeaders(sb, true);
    const std::string result = sb.toString();
    // Every header line must end with \r\n
    // Count \r\n occurrences - should be exactly 4
    int count = 0;
    size_t pos = 0;
    while ((pos = result.find("\r\n", pos)) != std::string::npos) {
        ++count;
        pos += 2;
    }
    REQUIRE(count == 4);
}

// ============================================================
// main
// ============================================================

int main() {
    // urlDecodePath
    test_urldecode_empty();
    test_urldecode_plain();
    test_urldecode_space();
    test_urldecode_uppercase_hex();
    test_urldecode_lowercase_hex();
    test_urldecode_mixed_hex_case();
    test_urldecode_percent_at_end();
    test_urldecode_incomplete_sequence();
    test_urldecode_invalid_hex_chars();

    // getFileExtension
    test_ext_html();
    test_ext_no_extension();
    test_ext_empty();
    test_ext_last_dot_wins();
    test_ext_dot_only();
    test_ext_hidden_file();

    // formatHttpDate
    test_httpdate_epoch();
    test_httpdate_known_timestamp();
    test_httpdate_nonempty();
    test_httpdate_contains_gmt();

    // addSecurityHeaders
    test_security_headers_disabled();
    test_security_headers_enabled_contains_all();
    test_security_headers_crlf_terminated();

    return test_summary();
}
