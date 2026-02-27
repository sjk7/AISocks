// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
//
// Tests for HttpRequest::parse()
//
// Coverage goals (grouped by RFC / spec area)
// ---------------------------------------------
// 1.  Basic GET -- method / path / version split
// 2.  All common methods (POST, PUT, DELETE, PATCH, HEAD, OPTIONS, TRACE)
// 3.  HTTP/1.0 vs HTTP/1.1 vs HTTP/2 version strings
// 4.  Path URL decoding -- rawPath preserved, path decoded
// 5.  '%2F' in path -- decoded in path but kept encoded in rawPath
// 6.  Query string split at first '?' only (second '?' belongs to value)
// 7.  Query parameters: single, multiple, ordering independent
// 8.  Query param: key-only (no '=' sign)
// 9.  Query param: empty value ('key=')
// 10. Query param: '+' decoded to space
// 11. Query param: '%2B' decoded to '+'
// 12. Query param: percent-encoded key
// 13. Header key case-folding (stored lowercase) -- RFC 7230 S.3.2
// 14. Header value OWS trimming (leading/trailing SP/HT) -- RFC 7230 S.3.2.6
// 15. Header value with colon -- only the first ':' splits key/value
// 16. Header with empty value ('X-Empty:')
// 17. Header with tab OWS
// 18. Case-insensitive header() accessor
// 19. headerOr() returns fallback for absent header
// 20. Body separated by \r\n\r\n
// 21. Body is empty when separator is absent body content is empty
// 22. POST with Content-Length header and body
// 23. Request with no headers (only request line + \r\n\r\n)
// 24. Absolute-form request target (HTTP/1.1 CONNECT / proxies)
// 25. Asterisk-form request target ('*')
// 26. Malformed: missing version -> valid=false
// 27. Malformed: missing SP after method -> valid=false
// 28. Malformed: completely empty string -> valid=false
// 29. operator bool reflects valid field
// 30. Multiple query params with the same key -- last value wins
// 31. Double-slash path
// 32. Root path '/'
// 33. Deep path with encoded characters
// 34. Fragment -- '#' is NOT sent in HTTP requests (but graceful if present)
// 35. Very long header value preserved intact
// 36. Request with many headers
// 37. Trailing whitespace on request line is handled gracefully

#include "HttpRequest.h"
#include "test_helpers.h"

#include <iostream>
#include <string>

using namespace aiSocks;

// -- helpers ----------------------------------------------------------------

// Build a minimal well-formed GET request.
static std::string makeGet(const std::string& target,
                           const std::string& version = "HTTP/1.1") {
    return "GET " + target + " " + version + "\r\n\r\n";
}

// Build a request with headers (each entry = "Key: Value").
static std::string makeRequest(const std::string& method,
                               const std::string& target,
                               const std::string& version,
                               const std::string& headers,
                               const std::string& body = {}) {
    std::string r = method + " " + target + " " + version + "\r\n";
    r += headers;
    if (!headers.empty() && (headers.size() < 2 ||
        headers.substr(headers.size() - 2) != "\r\n"))
        r += "\r\n";
    r += "\r\n";
    r += body;
    return r;
}

static void CHECK_FIELD(const std::string& label,
                        const std::string& got,
                        const std::string& expected) {
    REQUIRE_MSG(got == expected,
        label + ": expected \"" + expected + "\"  got \"" + got + "\"");
}

// -- test functions ---------------------------------------------------------

// 1. Basic GET
static void test_basic_get() {
    BEGIN_TEST("basic GET");
    auto req = HttpRequest::parse("GET /index.html HTTP/1.1\r\n\r\n");
    REQUIRE(req.valid);
    REQUIRE(static_cast<bool>(req));
    CHECK_FIELD("method",  req.method,  "GET");
    CHECK_FIELD("path",    req.path,    "/index.html");
    CHECK_FIELD("rawPath", req.rawPath, "/index.html");
    CHECK_FIELD("version", req.version, "HTTP/1.1");
    REQUIRE(req.queryString.empty());
    REQUIRE(req.queryParams.empty());
    REQUIRE(req.body.empty());
}

// 2. Common HTTP methods
static void test_methods() {
    BEGIN_TEST("HTTP methods");
    for (const std::string& m : {"POST", "PUT", "DELETE", "PATCH",
                                  "HEAD", "OPTIONS", "TRACE"}) {
        auto req = HttpRequest::parse(m + " / HTTP/1.1\r\n\r\n");
        REQUIRE_MSG(req.valid, "valid for method " + m);
        REQUIRE_MSG(req.method == m, "method field == " + m);
    }
}

// 3. HTTP version strings
static void test_versions() {
    BEGIN_TEST("HTTP versions");
    {
        auto req = HttpRequest::parse("GET / HTTP/1.0\r\n\r\n");
        REQUIRE(req.valid);
        CHECK_FIELD("version 1.0", req.version, "HTTP/1.0");
    }
    {
        auto req = HttpRequest::parse("GET / HTTP/1.1\r\n\r\n");
        REQUIRE(req.valid);
        CHECK_FIELD("version 1.1", req.version, "HTTP/1.1");
    }
    {
        auto req = HttpRequest::parse("GET / HTTP/2\r\n\r\n");
        REQUIRE(req.valid);
        CHECK_FIELD("version 2", req.version, "HTTP/2");
    }
}

// 4. Path URL decoding
static void test_path_decoding() {
    BEGIN_TEST("path URL decoding");
    {
        auto req = HttpRequest::parse("GET /hello%20world HTTP/1.1\r\n\r\n");
        REQUIRE(req.valid);
        CHECK_FIELD("rawPath", req.rawPath, "/hello%20world");
        CHECK_FIELD("path",    req.path,    "/hello world");
    }
    {
        // %41 = 'A'
        auto req = HttpRequest::parse("GET /fo%6F HTTP/1.1\r\n\r\n");
        REQUIRE(req.valid);
        CHECK_FIELD("rawPath %6F", req.rawPath, "/fo%6F");
        CHECK_FIELD("path %6F",    req.path,    "/foo");
    }
    {
        // Case-insensitive hex in path
        auto req = HttpRequest::parse("GET /%61%62%63 HTTP/1.1\r\n\r\n");
        REQUIRE(req.valid);
        CHECK_FIELD("path lowercase hex", req.path, "/abc");
    }
}

// 5. %2F in path -- encodes '/', should decode but rawPath preserved
static void test_path_encoded_slash() {
    BEGIN_TEST("%%2F in path");
    auto req = HttpRequest::parse("GET /foo%2Fbar HTTP/1.1\r\n\r\n");
    REQUIRE(req.valid);
    CHECK_FIELD("rawPath", req.rawPath, "/foo%2Fbar");
    CHECK_FIELD("path",    req.path,    "/foo/bar");
}

// 6. Query split at first '?' only
static void test_query_split() {
    BEGIN_TEST("query string split");
    {
        auto req = HttpRequest::parse("GET /path?a=1 HTTP/1.1\r\n\r\n");
        REQUIRE(req.valid);
        CHECK_FIELD("rawPath",     req.rawPath,     "/path");
        CHECK_FIELD("queryString", req.queryString, "a=1");
    }
    {
        // Second '?' belongs to the query value
        auto req = HttpRequest::parse("GET /path?a=1?b=2 HTTP/1.1\r\n\r\n");
        REQUIRE(req.valid);
        CHECK_FIELD("rawPath 2q",     req.rawPath,     "/path");
        CHECK_FIELD("queryString 2q", req.queryString, "a=1?b=2");
        REQUIRE(req.queryParams.count("a") == 1);
        CHECK_FIELD("param a value", req.queryParams.at("a"), "1?b=2");
    }
}

// 7. Multiple distinct query parameters
static void test_query_params_multiple() {
    BEGIN_TEST("multiple query params");
    auto req = HttpRequest::parse("GET /search?q=hello&page=2&sort=asc HTTP/1.1\r\n\r\n");
    REQUIRE(req.valid);
    REQUIRE(req.queryParams.size() == 3);
    CHECK_FIELD("q",    req.queryParams.at("q"),    "hello");
    CHECK_FIELD("page", req.queryParams.at("page"), "2");
    CHECK_FIELD("sort", req.queryParams.at("sort"), "asc");
}

// 8. Query param: key only (no '=')
static void test_query_param_key_only() {
    BEGIN_TEST("query param key-only");
    auto req = HttpRequest::parse("GET /?verbose HTTP/1.1\r\n\r\n");
    REQUIRE(req.valid);
    REQUIRE(req.queryParams.count("verbose") == 1);
    CHECK_FIELD("verbose value", req.queryParams.at("verbose"), "");
}

// 9. Query param: empty value
static void test_query_param_empty_value() {
    BEGIN_TEST("query param empty value");
    auto req = HttpRequest::parse("GET /?token= HTTP/1.1\r\n\r\n");
    REQUIRE(req.valid);
    REQUIRE(req.queryParams.count("token") == 1);
    CHECK_FIELD("token empty", req.queryParams.at("token"), "");
}

// 10. Query param: '+' decoded to space (form-encoding convention)
static void test_query_param_plus_space() {
    BEGIN_TEST("query param '+' as space");
    auto req = HttpRequest::parse("GET /?q=hello+world HTTP/1.1\r\n\r\n");
    REQUIRE(req.valid);
    CHECK_FIELD("q+space", req.queryParams.at("q"), "hello world");
}

// 11. Query param: %2B decoded to '+'
static void test_query_param_encoded_plus() {
    BEGIN_TEST("query param %2B as '+'");
    auto req = HttpRequest::parse("GET /?sign=C%2B%2B HTTP/1.1\r\n\r\n");
    REQUIRE(req.valid);
    CHECK_FIELD("sign C++", req.queryParams.at("sign"), "C++");
}

// 12. Query param: encoded key
static void test_query_param_encoded_key() {
    BEGIN_TEST("query param encoded key");
    auto req = HttpRequest::parse("GET /?hello%20world=1 HTTP/1.1\r\n\r\n");
    REQUIRE(req.valid);
    REQUIRE(req.queryParams.count("hello world") == 1);
    CHECK_FIELD("encoded key", req.queryParams.at("hello world"), "1");
}

// 13. Header key case-folding (RFC 7230 S.3.2)
static void test_header_case_folding() {
    BEGIN_TEST("header key case-folding");
    std::string raw =
        "GET / HTTP/1.1\r\n"
        "Content-Type: text/html\r\n"
        "X-My-Header: value\r\n"
        "ACCEPT: application/json\r\n"
        "\r\n";
    auto req = HttpRequest::parse(raw);
    REQUIRE(req.valid);
    REQUIRE(req.headers.count("content-type") == 1);
    REQUIRE(req.headers.count("x-my-header")  == 1);
    REQUIRE(req.headers.count("accept")        == 1);
    // Original-case keys should NOT exist
    REQUIRE(req.headers.count("Content-Type") == 0);
    REQUIRE(req.headers.count("ACCEPT")        == 0);
    CHECK_FIELD("content-type", req.headers.at("content-type"), "text/html");
}

// 14. Header OWS trimming (RFC 7230 S.3.2.6)
static void test_header_ows() {
    BEGIN_TEST("header OWS trimming");
    std::string raw =
        "GET / HTTP/1.1\r\n"
        "Content-Type:   text/html   \r\n"
        "X-A:value_no_space\r\n"
        "X-B:  leading only\r\n"
        "\r\n";
    auto req = HttpRequest::parse(raw);
    REQUIRE(req.valid);
    CHECK_FIELD("OWS both sides",  req.headers.at("content-type"), "text/html");
    CHECK_FIELD("no spaces",       req.headers.at("x-a"), "value_no_space");
    CHECK_FIELD("leading only",    req.headers.at("x-b"), "leading only");
}

// 15. Header value with colon -- only first colon splits key/value
static void test_header_colon_in_value() {
    BEGIN_TEST("colon in header value");
    std::string raw =
        "GET / HTTP/1.1\r\n"
        "Authorization: Basic dXNlcjpwYXNz\r\n"
        "Date: Mon, 01 Jan 2024 00:00:00 GMT\r\n"
        "\r\n";
    auto req = HttpRequest::parse(raw);
    REQUIRE(req.valid);
    CHECK_FIELD("Authorization", req.headers.at("authorization"),
                "Basic dXNlcjpwYXNz");
    CHECK_FIELD("Date with colon", req.headers.at("date"),
                "Mon, 01 Jan 2024 00:00:00 GMT");
}

// 16. Header with empty value
static void test_header_empty_value() {
    BEGIN_TEST("header with empty value");
    std::string raw =
        "GET / HTTP/1.1\r\n"
        "X-Empty:\r\n"
        "X-Spaces:   \r\n"
        "\r\n";
    auto req = HttpRequest::parse(raw);
    REQUIRE(req.valid);
    CHECK_FIELD("x-empty",  req.headers.at("x-empty"),  "");
    CHECK_FIELD("x-spaces", req.headers.at("x-spaces"), "");
}

// 17. Header with tab OWS (RFC 7230 allows HTAB as OWS)
static void test_header_tab_ows() {
    BEGIN_TEST("header tab OWS");
    std::string raw =
        "GET / HTTP/1.1\r\n"
        "Content-Type:\ttext/plain\t\r\n"
        "\r\n";
    auto req = HttpRequest::parse(raw);
    REQUIRE(req.valid);
    CHECK_FIELD("tab OWS", req.headers.at("content-type"), "text/plain");
}

// 18. header() accessor is case-insensitive
static void test_header_accessor_case() {
    BEGIN_TEST("header() accessor case-insensitivity");
    std::string raw =
        "GET / HTTP/1.1\r\n"
        "Content-Length: 42\r\n"
        "\r\n";
    auto req = HttpRequest::parse(raw);
    REQUIRE(req.valid);
    REQUIRE(req.header("content-length")  != nullptr);
    REQUIRE(req.header("Content-Length")  != nullptr);
    REQUIRE(req.header("CONTENT-LENGTH")  != nullptr);
    REQUIRE(req.header("Content-length")  != nullptr);
    CHECK_FIELD("via accessor", *req.header("Content-Length"), "42"); //-V522
    REQUIRE(req.header("x-not-present") == nullptr);
}

// 19. headerOr() fallback
static void test_header_or_fallback() {
    BEGIN_TEST("headerOr() fallback");
    auto req = HttpRequest::parse("GET / HTTP/1.1\r\n\r\n");
    REQUIRE(req.valid);
    CHECK_FIELD("absent fallback", req.headerOr("x-missing", "default"), "default");
    CHECK_FIELD("absent empty",    req.headerOr("x-missing"),            "");
}

// 20. Body separated by \r\n\r\n
static void test_body_separation() {
    BEGIN_TEST("body separation");
    std::string raw =
        "POST /submit HTTP/1.1\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\n"
        "\r\n"
        "name=Alice&age=30";
    auto req = HttpRequest::parse(raw);
    REQUIRE(req.valid);
    CHECK_FIELD("body", req.body, "name=Alice&age=30");
}

// 21. No body -> body is empty
static void test_no_body() {
    BEGIN_TEST("no body");
    auto req = HttpRequest::parse("GET / HTTP/1.1\r\nHost: example.com\r\n\r\n");
    REQUIRE(req.valid);
    REQUIRE(req.body.empty());
}

// 22. POST with Content-Length and body
static void test_post_with_body() {
    BEGIN_TEST("POST with body");
    std::string body = "{\"key\":\"value\"}";
    std::string raw =
        "POST /api/data HTTP/1.1\r\n"
        "Host: api.example.com\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "\r\n" + body;
    auto req = HttpRequest::parse(raw);
    REQUIRE(req.valid);
    CHECK_FIELD("POST method", req.method, "POST");
    CHECK_FIELD("POST path",   req.path,   "/api/data");
    CHECK_FIELD("POST body",   req.body,   body);
    CHECK_FIELD("Content-Length header",
                *req.header("content-length"), //-V522
                std::to_string(body.size()));
}

// 23. Request with no headers (bare request line)
static void test_no_headers() {
    BEGIN_TEST("no headers");
    auto req = HttpRequest::parse("GET / HTTP/1.1\r\n\r\n");
    REQUIRE(req.valid);
    REQUIRE(req.headers.empty());
    CHECK_FIELD("path", req.path, "/");
}

// 24. Absolute-form target (proxy / CONNECT)
static void test_absolute_form() {
    BEGIN_TEST("absolute-form target");
    auto req = HttpRequest::parse(
        "GET http://example.com/page?id=1 HTTP/1.1\r\nHost: example.com\r\n\r\n");
    REQUIRE(req.valid);
    CHECK_FIELD("rawPath abs",     req.rawPath,     "http://example.com/page");
    CHECK_FIELD("queryString abs", req.queryString, "id=1");
    CHECK_FIELD("method abs",      req.method,      "GET");
}

// 25. Asterisk-form target (OPTIONS *)
static void test_asterisk_form() {
    BEGIN_TEST("asterisk-form target");
    auto req = HttpRequest::parse("OPTIONS * HTTP/1.1\r\n\r\n");
    REQUIRE(req.valid);
    CHECK_FIELD("method *",  req.method,  "OPTIONS");
    CHECK_FIELD("rawPath *", req.rawPath, "*");
    CHECK_FIELD("path *",    req.path,    "*");
}

// 26. Malformed: missing HTTP version
static void test_malformed_no_version() {
    BEGIN_TEST("malformed: missing version");
    auto req = HttpRequest::parse("GET /path\r\n\r\n");
    REQUIRE(!req.valid);
    REQUIRE(!static_cast<bool>(req));
}

// 27. Malformed: only one token
static void test_malformed_one_token() {
    BEGIN_TEST("malformed: one token");
    auto req = HttpRequest::parse("BADREQUEST\r\n\r\n");
    REQUIRE(!req.valid);
}

// 28. Malformed: empty string
static void test_malformed_empty() {
    BEGIN_TEST("malformed: empty string");
    auto req = HttpRequest::parse("");
    REQUIRE(!req.valid);
}

// 29. operator bool
static void test_operator_bool() {
    BEGIN_TEST("operator bool");
    REQUIRE( static_cast<bool>(HttpRequest::parse("GET / HTTP/1.1\r\n\r\n")));
    REQUIRE(!static_cast<bool>(HttpRequest::parse("")));
    REQUIRE(!static_cast<bool>(HttpRequest::parse("OOPS\r\n\r\n")));
}

// 30. Duplicate query param key -- last value wins
static void test_duplicate_query_key() {
    BEGIN_TEST("duplicate query param key");
    // RFC 3986 does not define which value wins; common behaviour is last-wins.
    auto req = HttpRequest::parse("GET /?color=red&color=blue HTTP/1.1\r\n\r\n");
    REQUIRE(req.valid);
    REQUIRE(req.queryParams.count("color") == 1);
    CHECK_FIELD("duplicate key last-wins", req.queryParams.at("color"), "blue");
}

// 31. Double-slash path
static void test_double_slash_path() {
    BEGIN_TEST("double-slash path");
    auto req = HttpRequest::parse("GET //double/slash HTTP/1.1\r\n\r\n");
    REQUIRE(req.valid);
    CHECK_FIELD("double-slash", req.rawPath, "//double/slash");
    CHECK_FIELD("double-slash decoded", req.path, "//double/slash");
}

// 32. Root path
static void test_root_path() {
    BEGIN_TEST("root path '/'");
    auto req = HttpRequest::parse("GET / HTTP/1.1\r\n\r\n");
    REQUIRE(req.valid);
    CHECK_FIELD("root path", req.path, "/");
    REQUIRE(req.queryString.empty());
}

// 33. Deep path with encoded spaces
static void test_deep_encoded_path() {
    BEGIN_TEST("deep path with encoded chars");
    auto req = HttpRequest::parse(
        "GET /api/v1/users/John%20Doe/profile?lang=en HTTP/1.1\r\n\r\n");
    REQUIRE(req.valid);
    CHECK_FIELD("rawPath deep", req.rawPath, "/api/v1/users/John%20Doe/profile");
    CHECK_FIELD("path deep",    req.path,    "/api/v1/users/John Doe/profile");
    CHECK_FIELD("lang param",   req.queryParams.at("lang"), "en");
}

// 34. '#' fragment -- clients MUST NOT send fragments; if present, it ends up
//     in the query string or param value (graceful pass-through expected)
static void test_fragment_not_sent() {
    BEGIN_TEST("fragment (graceful pass-through)");
    // In a real HTTP exchange this never happens, but a fuzzer might send it.
    auto req = HttpRequest::parse("GET /page?q=foo#section HTTP/1.1\r\n\r\n");
    REQUIRE(req.valid);
    // Fragment becomes part of last param value (common lenient behaviour)
    REQUIRE(req.queryParams.count("q") == 1);
    REQUIRE_MSG(req.queryParams.at("q").find("foo") != std::string::npos,
        "query param 'q' contains 'foo' even with fragment present");
}

// 35. Long header value preserved intact
static void test_long_header_value() {
    BEGIN_TEST("long header value");
    std::string longVal(4096, 'x');
    std::string raw =
        "GET / HTTP/1.1\r\n"
        "X-Long: " + longVal + "\r\n"
        "\r\n";
    auto req = HttpRequest::parse(raw);
    REQUIRE(req.valid);
    CHECK_FIELD("long value", req.headers.at("x-long"), longVal);
}

// 36. Many headers
static void test_many_headers() {
    BEGIN_TEST("many headers");
    std::string raw = "GET / HTTP/1.1\r\n";
    for (int i = 0; i < 50; ++i)
        raw += "X-H" + std::to_string(i) + ": v" + std::to_string(i) + "\r\n";
    raw += "\r\n";
    auto req = HttpRequest::parse(raw);
    REQUIRE(req.valid);
    REQUIRE_MSG(req.headers.size() == 50, "50 headers parsed");
    CHECK_FIELD("header 0",  req.headers.at("x-h0"),  "v0");
    CHECK_FIELD("header 49", req.headers.at("x-h49"), "v49");
}

// 37. Typical browser-like request (integration)
static void test_typical_browser_request() {
    BEGIN_TEST("typical browser request");
    std::string raw =
        "GET /search?q=c%2B%2B+templates&safe=off HTTP/1.1\r\n"
        "Host: www.example.com\r\n"
        "User-Agent: Mozilla/5.0\r\n"
        "Accept: text/html,application/xhtml+xml\r\n"
        "Accept-Language: en-US,en;q=0.9\r\n"
        "Accept-Encoding: gzip, deflate, br\r\n"
        "Connection: keep-alive\r\n"
        "\r\n";
    auto req = HttpRequest::parse(raw);
    REQUIRE(req.valid);
    CHECK_FIELD("browser method",  req.method,  "GET");
    CHECK_FIELD("browser path",    req.path,    "/search");
    CHECK_FIELD("browser version", req.version, "HTTP/1.1");
    CHECK_FIELD("q param", req.queryParams.at("q"),    "c++ templates");
    CHECK_FIELD("safe param", req.queryParams.at("safe"), "off");
    CHECK_FIELD("host header",       req.headers.at("host"),     "www.example.com");
    CHECK_FIELD("user-agent header", *req.header("User-Agent"),  "Mozilla/5.0"); //-V522
    CHECK_FIELD("connection header", req.headers.at("connection"), "keep-alive");
    REQUIRE(req.body.empty());
}

// -- main -------------------------------------------------------------------

int main() {
    test_basic_get();
    test_methods();
    test_versions();
    test_path_decoding();
    test_path_encoded_slash();
    test_query_split();
    test_query_params_multiple();
    test_query_param_key_only();
    test_query_param_empty_value();
    test_query_param_plus_space();
    test_query_param_encoded_plus();
    test_query_param_encoded_key();
    test_header_case_folding();
    test_header_ows();
    test_header_colon_in_value();
    test_header_empty_value();
    test_header_tab_ows();
    test_header_accessor_case();
    test_header_or_fallback();
    test_body_separation();
    test_no_body();
    test_post_with_body();
    test_no_headers();
    test_absolute_form();
    test_asterisk_form();
    test_malformed_no_version();
    test_malformed_one_token();
    test_malformed_empty();
    test_operator_bool();
    test_duplicate_query_key();
    test_double_slash_path();
    test_root_path();
    test_deep_encoded_path();
    test_fragment_not_sent();
    test_long_header_value();
    test_many_headers();
    test_typical_browser_request();
    return test_summary();
}
