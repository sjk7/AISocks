// Tests for ClientHttpRequest::Builder, forUrl(), and forPost().
//
// Coverage:
// 1.  forUrl() produces a valid GET request with all default headers
// 2.  forUrl() sets correct Host from URL
// 3.  forUrl() sets correct path from URL
// 4.  forUrl() with non-standard port includes port in Host header
// 5.  forUrl() with no path defaults to "/"
// 6.  forPost() produces a POST request with body and Content-Length
// 7.  forPost() sets Content-Type header
// 8.  forPost() with custom content type
// 9.  builder() custom User-Agent overrides default
// 10. builder() custom Accept overrides default
// 11. builder() with empty accept omits Accept header
// 12. builder() extra custom headers are included
// 13. builder() PUT with body emits Content-Length
// 14. builder() GET with body does NOT emit Content-Length
// 15. forUrl() output is parseable by HttpRequest::parse()

#include "ClientHttpRequest.h"
#include "HttpRequest.h"
#include "test_helpers.h"

#include <string>
#include <string_view>

using namespace aiSocks;

static bool contains(const std::string& s, std::string_view sub) {
    return s.find(sub) != std::string::npos;
}

static bool startsWith(const std::string& s, std::string_view prefix) {
    return s.rfind(prefix, 0) == 0;
}

// ---------------------------------------------------------------------------
// 1. forUrl() produces a complete GET request with all default headers
// ---------------------------------------------------------------------------
static void test_forUrl_defaults() {
    BEGIN_TEST("forUrl() default headers");
    std::string req = ClientHttpRequest::forUrl("http://example.com/");
    REQUIRE(startsWith(req, "GET / HTTP/1.1\r\n"));
    REQUIRE(contains(req, "Host: example.com\r\n"));
    REQUIRE(contains(req, "User-Agent: AISocks-HttpClient/1.0\r\n"));
    REQUIRE(contains(req, "Accept: */*\r\n"));
    REQUIRE(contains(req, "Connection: close\r\n"));
    REQUIRE(contains(req, "\r\n\r\n"));
}

// ---------------------------------------------------------------------------
// 2. forUrl() sets correct Host
// ---------------------------------------------------------------------------
static void test_forUrl_host() {
    BEGIN_TEST("forUrl() Host header");
    std::string req = ClientHttpRequest::forUrl("http://api.example.org/data");
    REQUIRE(contains(req, "Host: api.example.org\r\n"));
}

// ---------------------------------------------------------------------------
// 3. forUrl() sets correct path
// ---------------------------------------------------------------------------
static void test_forUrl_path() {
    BEGIN_TEST("forUrl() request path");
    std::string req
        = ClientHttpRequest::forUrl("http://example.com/api/v1/items?page=2");
    REQUIRE(startsWith(req, "GET /api/v1/items?page=2 HTTP/1.1\r\n"));
}

// ---------------------------------------------------------------------------
// 4. forUrl() with non-standard port includes port in Host
// ---------------------------------------------------------------------------
static void test_forUrl_port_in_host() {
    BEGIN_TEST("forUrl() non-standard port in Host");
    std::string req = ClientHttpRequest::forUrl("http://example.com:8080/path");
    REQUIRE(contains(req, "Host: example.com:8080\r\n"));
    REQUIRE(startsWith(req, "GET /path HTTP/1.1\r\n"));
}

// ---------------------------------------------------------------------------
// 5. forUrl() with no explicit path defaults to "/"
// ---------------------------------------------------------------------------
static void test_forUrl_no_path_defaults_to_root() {
    BEGIN_TEST("forUrl() no path → /");
    std::string req = ClientHttpRequest::forUrl("http://example.com");
    REQUIRE(startsWith(req, "GET / HTTP/1.1\r\n"));
}

// ---------------------------------------------------------------------------
// 6. forPost() produces POST with body and Content-Length
// ---------------------------------------------------------------------------
static void test_forPost_body_and_content_length() {
    BEGIN_TEST("forPost() body + Content-Length");
    std::string req
        = ClientHttpRequest::forPost("http://example.com/submit", "key=value");
    REQUIRE(startsWith(req, "POST /submit HTTP/1.1\r\n"));
    REQUIRE(contains(req, "Content-Length: 9\r\n"));
    // Body must appear after the blank line
    const auto blankLine = req.find("\r\n\r\n");
    REQUIRE(blankLine != std::string::npos);
    REQUIRE(req.substr(blankLine + 4) == "key=value");
}

// ---------------------------------------------------------------------------
// 7. forPost() sets default Content-Type
// ---------------------------------------------------------------------------
static void test_forPost_default_content_type() {
    BEGIN_TEST("forPost() default Content-Type");
    std::string req = ClientHttpRequest::forPost("http://example.com/", "x=1");
    REQUIRE(
        contains(req, "Content-Type: application/x-www-form-urlencoded\r\n"));
}

// ---------------------------------------------------------------------------
// 8. forPost() with custom Content-Type
// ---------------------------------------------------------------------------
static void test_forPost_custom_content_type() {
    BEGIN_TEST("forPost() custom Content-Type");
    std::string req = ClientHttpRequest::forPost(
        "http://example.com/api", R"({"k":1})", "application/json");
    REQUIRE(contains(req, "Content-Type: application/json\r\n"));
}

// ---------------------------------------------------------------------------
// 9. builder() custom User-Agent
// ---------------------------------------------------------------------------
static void test_builder_custom_user_agent() {
    BEGIN_TEST("builder() custom User-Agent");
    std::string req = ClientHttpRequest::builder()
                          .url("http://example.com/")
                          .userAgent("MyApp/2.0")
                          .build();
    REQUIRE(contains(req, "User-Agent: MyApp/2.0\r\n"));
    REQUIRE(!contains(req, "AISocks-HttpClient"));
}

// ---------------------------------------------------------------------------
// 10. builder() custom Accept
// ---------------------------------------------------------------------------
static void test_builder_custom_accept() {
    BEGIN_TEST("builder() custom Accept");
    std::string req = ClientHttpRequest::builder()
                          .url("http://example.com/")
                          .accept("text/html,application/xhtml+xml")
                          .build();
    REQUIRE(contains(req, "Accept: text/html,application/xhtml+xml\r\n"));
}

// ---------------------------------------------------------------------------
// 11. builder() empty accept omits Accept header
// ---------------------------------------------------------------------------
static void test_builder_empty_accept_omitted() {
    BEGIN_TEST("builder() empty accept omits header");
    std::string req = ClientHttpRequest::builder()
                          .url("http://example.com/")
                          .accept("")
                          .build();
    REQUIRE(!contains(req, "Accept:"));
}

// ---------------------------------------------------------------------------
// 12. builder() custom headers are included
// ---------------------------------------------------------------------------
static void test_builder_custom_headers() {
    BEGIN_TEST("builder() custom headers");
    std::string req = ClientHttpRequest::builder()
                          .url("http://example.com/")
                          .header("X-Api-Key", "secret123")
                          .header("X-Request-Id", "abc-456")
                          .build();
    REQUIRE(contains(req, "X-Api-Key: secret123\r\n"));
    REQUIRE(contains(req, "X-Request-Id: abc-456\r\n"));
}

// ---------------------------------------------------------------------------
// 13. builder() PUT with body emits Content-Length
// ---------------------------------------------------------------------------
static void test_builder_put_content_length() {
    BEGIN_TEST("builder() PUT Content-Length");
    std::string body = "hello";
    std::string req = ClientHttpRequest::builder()
                          .method("PUT")
                          .url("http://example.com/resource")
                          .body(body)
                          .build();
    REQUIRE(startsWith(req, "PUT /resource HTTP/1.1\r\n"));
    REQUIRE(contains(req, "Content-Length: 5\r\n"));
    const auto sep = req.find("\r\n\r\n");
    REQUIRE(sep != std::string::npos);
    REQUIRE(req.substr(sep + 4) == "hello");
}

// ---------------------------------------------------------------------------
// 14. builder() GET with body does NOT emit Content-Length
// ---------------------------------------------------------------------------
static void test_builder_get_no_content_length() {
    BEGIN_TEST("builder() GET body excluded from Content-Length");
    std::string req = ClientHttpRequest::builder()
                          .url("http://example.com/")
                          .body("should-be-ignored-for-content-length")
                          .build();
    REQUIRE(!contains(req, "Content-Length:"));
}

// ---------------------------------------------------------------------------
// 15. forUrl() output is parseable by HttpRequest::parse()
// ---------------------------------------------------------------------------
static void test_forUrl_parseable() {
    BEGIN_TEST("forUrl() output parseable by HttpRequest::parse()");
    std::string raw
        = ClientHttpRequest::forUrl("http://example.com/search?q=test");
    auto req = HttpRequest::parse(raw);
    REQUIRE(req.valid);
    REQUIRE(req.method == "GET");
    REQUIRE(req.path == "/search");
    REQUIRE(req.queryString == "q=test");
    REQUIRE(req.queryParams.at("q") == "test");
    REQUIRE(req.headerOr("host") == "example.com");
    REQUIRE(req.headerOr("user-agent") == "AISocks-HttpClient/1.0");
    REQUIRE(req.headerOr("accept") == "*/*");
    REQUIRE(req.headerOr("connection") == "close");
}

// ---------------------------------------------------------------------------
int main() {
    test_forUrl_defaults();
    test_forUrl_host();
    test_forUrl_path();
    test_forUrl_port_in_host();
    test_forUrl_no_path_defaults_to_root();
    test_forPost_body_and_content_length();
    test_forPost_default_content_type();
    test_forPost_custom_content_type();
    test_builder_custom_user_agent();
    test_builder_custom_accept();
    test_builder_empty_accept_omitted();
    test_builder_custom_headers();
    test_builder_put_content_length();
    test_builder_get_no_content_length();
    test_forUrl_parseable();
    return test_summary();
}
