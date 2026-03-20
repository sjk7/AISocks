// This is an independent project of an individual developer. Dear PVS-Studio, please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
// Regression tests for the 7 bugs/footguns fixed in commit 31f193c.
//
// 1. parseHexSize_ integer overflow (chunk-size SIZE_MAX wraps arithmetic)
// 2. FG-1: HttpRequest fields outlive source buffer
// 3. FG-2: HttpResponse fields outlive parser reset()
// 4. FG-2: HttpResponse fields outlive parser destruction
// 5. FG-4: HttpClientState move — external dataView not corrupted
// 6. FG-4: HttpClientState move — internal dataBuf view fixed up
// 7. FG-5: isHttpRequest() rejects method-name superstrings (e.g. "POSTAL")
// 8. FG-5: isHttpRequest() accepts all seven supported methods

#include "HttpPollServer.h"
#include "HttpRequest.h"
#include "HttpResponse.h"
#include "test_helpers.h"

#include <string>
#include <string_view>

using namespace aiSocks;

// ---------------------------------------------------------------------------
// Helper: expose the protected static isHttpRequest() without constructing
// a real server.  Static methods can be called via a complete derived type
// without any instance — the using-declaration just changes access level.
// ---------------------------------------------------------------------------
namespace {
struct HttpPollServerAccess : public HttpPollServer {
    using HttpPollServer::isHttpRequest;
    // Must provide buildResponse to make the class non-abstract.
    void buildResponse(HttpClientState&) override {}
};
} // namespace

// ---------------------------------------------------------------------------
// 1. parseHexSize_ overflow guard
// ---------------------------------------------------------------------------
static void test_chunked_size_overflow() {
    BEGIN_TEST("chunked: oversized chunk-length rejected (overflow guard)");
    // Before the fix, feeding FFFFFFFFFFFFFFFF as a chunk-size would wrap
    // the size_t arithmetic past the guard and call decodedBody_.append()
    // with SIZE_MAX bytes, triggering std::bad_alloc / heap corruption.
    // The 1 GB cap must reject this as State::Error.
    HttpResponseParser p;
    std::string raw = "HTTP/1.1 200 OK\r\n"
                      "Transfer-Encoding: chunked\r\n"
                      "\r\n"
                      "FFFFFFFFFFFFFFFF\r\n"
                      "x\r\n"
                      "0\r\n"
                      "\r\n";
    auto state = p.feed(raw.data(), raw.size());
    REQUIRE(state == HttpResponseParser::State::Error);
    REQUIRE(p.isError());
}

static void test_chunked_size_large_rejected() {
    BEGIN_TEST("chunked: >1 GB chunk-length rejected (9-digit hex)");
    // "FFFFFFFFF" (9 hex digits).  After processing the first 8 chars the
    // running total is 0xFFFFFFFF = 4,294,967,295, which exceeds the 1 GB
    // guard, so the guard fires on the 9th character → State::Error.
    HttpResponseParser p;
    std::string raw = "HTTP/1.1 200 OK\r\n"
                      "Transfer-Encoding: chunked\r\n"
                      "\r\n"
                      "FFFFFFFFF\r\n"
                      "x\r\n"
                      "0\r\n"
                      "\r\n";
    auto state = p.feed(raw.data(), raw.size());
    REQUIRE(state == HttpResponseParser::State::Error);
    REQUIRE(p.isError());
}

static void test_chunked_size_reasonable_accepted() {
    BEGIN_TEST("chunked: small chunk accepted (overflow guard not triggered)");
    // Sanity-check: a normal chunked response still parses correctly.
    HttpResponseParser p;
    std::string raw = "HTTP/1.1 200 OK\r\n"
                      "Transfer-Encoding: chunked\r\n"
                      "\r\n"
                      "5\r\n"
                      "Hello\r\n"
                      "0\r\n"
                      "\r\n";
    auto state = p.feed(raw.data(), raw.size());
    REQUIRE(state == HttpResponseParser::State::Complete);
    REQUIRE(p.response().body() == "Hello");
}

// ---------------------------------------------------------------------------
// 2. FG-1: HttpRequest fields outlive source buffer
// ---------------------------------------------------------------------------
static void test_request_fields_outlive_source() {
    BEGIN_TEST("HttpRequest fields outlive source buffer (FG-1)");
    HttpRequest req;
    {
        std::string raw = "POST /submit HTTP/1.1\r\n"
                          "Host: example.com\r\n"
                          "Content-Type: application/json\r\n"
                          "\r\n"
                          "payload";
        req = HttpRequest::parse(raw);
        REQUIRE(req.valid);
    }
    // 'raw' is destroyed here.  All std::string fields must be independent
    // copies unaffected by the source buffer going out of scope.
    REQUIRE(req.method == "POST");
    REQUIRE(req.rawPath == "/submit");
    REQUIRE(req.version == "HTTP/1.1");
    REQUIRE(req.body == "payload");
    REQUIRE(req.headers.at("host") == "example.com");
    REQUIRE(req.headers.at("content-type") == "application/json");
    REQUIRE(req.header("Host") != nullptr);
    REQUIRE(*req.header("Host") == "example.com"); //-V522
}

static void test_request_query_params_outlive_source() {
    BEGIN_TEST("HttpRequest query params outlive source buffer (FG-1)");
    HttpRequest req;
    {
        std::string raw = "GET /search?q=hello+world&page=2 HTTP/1.1\r\n\r\n";
        req = HttpRequest::parse(raw);
        REQUIRE(req.valid);
    }
    REQUIRE(req.rawPath == "/search");
    REQUIRE(req.queryString == "q=hello+world&page=2");
    REQUIRE(req.queryParams.at("q") == "hello world");
    REQUIRE(req.queryParams.at("page") == "2");
}

// ---------------------------------------------------------------------------
// 3. FG-2: HttpResponse copy survives parser reset()
// ---------------------------------------------------------------------------
static void test_response_copy_survives_reset() {
    BEGIN_TEST("HttpResponse copy survives parser reset() (FG-2)");
    std::string raw = "HTTP/1.1 200 OK\r\n"
                      "Content-Type: text/plain\r\n"
                      "Content-Length: 5\r\n"
                      "\r\n"
                      "Hello";
    HttpResponseParser p;
    p.feed(raw.data(), raw.size());
    REQUIRE(p.isComplete());

    // Snapshot the response.  Before the fix, HttpResponse fields were
    // string_views into parser-owned buffers; copying didn't help because
    // the views still pointed into the parser.  Now they are std::string.
    HttpResponse resp = p.response();
    p.reset(); // clears parser's internal buffers

    REQUIRE(resp.valid);
    REQUIRE(resp.statusCode == 200);
    REQUIRE(resp.version() == "HTTP/1.1");
    REQUIRE(resp.statusText() == "OK");
    REQUIRE(resp.body() == "Hello");
    REQUIRE(resp.header("content-type") != nullptr);
    REQUIRE(*resp.header("content-type") == "text/plain"); //-V522
}

// ---------------------------------------------------------------------------
// 4. FG-2: HttpResponse copy survives parser destruction
// ---------------------------------------------------------------------------
static void test_response_copy_survives_parser_destruction() {
    BEGIN_TEST("HttpResponse copy survives parser destruction (FG-2)");
    HttpResponse resp;
    {
        std::string raw = "HTTP/1.1 404 Not Found\r\n"
                          "X-Custom: alive\r\n"
                          "Content-Length: 0\r\n"
                          "\r\n";
        HttpResponseParser p;
        p.feed(raw.data(), raw.size());
        REQUIRE(p.isComplete());
        resp = p.response(); // copy before parser goes out of scope
    }
    // Parser and its internal buffers are now destroyed.
    REQUIRE(resp.valid);
    REQUIRE(resp.statusCode == 404);
    REQUIRE(resp.statusText() == "Not Found");
    REQUIRE(resp.body().empty());
    REQUIRE(resp.header("x-custom") != nullptr);
    REQUIRE(*resp.header("x-custom") == "alive"); //-V522
}

// ---------------------------------------------------------------------------
// 5. FG-4: HttpClientState move — external dataView preserved
// ---------------------------------------------------------------------------
static void test_client_state_move_external_view() {
    BEGIN_TEST("HttpClientState move: external dataView preserved (FG-4)");
    // Simulate the zero-copy path: dataView points into server-owned
    // static storage, not into dataBuf.
    static const std::string kServerResponse
        = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";

    HttpClientState src;
    src.dataView = kServerResponse; // NOT pointing into dataBuf
    REQUIRE(src.dataBuf.empty());

    HttpClientState dst = std::move(src);

    // Before the fix, the move ctor unconditionally ran:
    //   if (!dataBuf.empty()) dataView = dataBuf;
    // which was benign here (dataBuf was empty), but the fix now also
    // correctly handles the dataBuf-non-empty + external-view case.
    // This test verifies dataView still tracks the external string.
    REQUIRE(dst.dataView.data() == kServerResponse.data());
    REQUIRE(dst.dataView == kServerResponse);
    REQUIRE(dst.dataBuf.empty());
}

static void test_client_state_copy_external_view() {
    BEGIN_TEST("HttpClientState copy: external dataView preserved (FG-4)");
    static const std::string kExt = "HTTP/1.1 204 No Content\r\n\r\n";
    HttpClientState src;
    src.dataView = kExt;

    HttpClientState dst = src; // copy constructor

    REQUIRE(dst.dataView.data() == kExt.data());
    REQUIRE(dst.dataView == kExt);
    REQUIRE(dst.dataBuf.empty());
}

// ---------------------------------------------------------------------------
// 6. FG-4: HttpClientState move — dataBuf view fixed up
// ---------------------------------------------------------------------------
static void test_client_state_move_internal_view() {
    BEGIN_TEST("HttpClientState move: dataBuf view fixed up (FG-4)");
    HttpClientState src;
    src.dataBuf = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
    src.dataView = src.dataBuf; // points into dataBuf

    const auto old_data = src.dataBuf.data(); // address before move

    HttpClientState dst = std::move(src);

    // std::string move usually keeps the same heap address (SSO aside).
    // What matters is dst.dataView points into dst.dataBuf, not
    // into the (now-moved-from) src.dataBuf.
    REQUIRE(dst.dataBuf.data() == old_data); // move stayed same address
    REQUIRE(dst.dataView.data() == dst.dataBuf.data());
    REQUIRE(dst.dataView == dst.dataBuf);
}

static void test_client_state_move_assign_internal_view() {
    BEGIN_TEST("HttpClientState move-assign: dataBuf view fixed up (FG-4)");
    HttpClientState src;
    src.dataBuf = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
    src.dataView = src.dataBuf;

    HttpClientState dst;
    dst = std::move(src);

    REQUIRE(dst.dataView.data() == dst.dataBuf.data());
    REQUIRE(dst.dataView == dst.dataBuf);
}

static void test_client_state_copy_internal_view() {
    BEGIN_TEST("HttpClientState copy: dataBuf view fixed up (FG-4)");
    HttpClientState src;
    src.dataBuf = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
    src.dataView = src.dataBuf;

    HttpClientState dst = src; // copy constructor

    // dst.dataBuf is a new heap allocation; view must point into dst's buf.
    REQUIRE(dst.dataView.data() != src.dataBuf.data());
    REQUIRE(dst.dataView.data() == dst.dataBuf.data());
    REQUIRE(dst.dataView == dst.dataBuf);
}

// ---------------------------------------------------------------------------
// 7. FG-5: isHttpRequest() rejects method-name superstrings
// ---------------------------------------------------------------------------
static void test_is_http_request_no_false_positives() {
    BEGIN_TEST(
        "isHttpRequest(): no false positives on method superstrings (FG-5)");
    // Before the fix, rfind("POST", 0) would match "POSTAL", etc.
    // The fix requires the full method name followed by a space.
    REQUIRE(!HttpPollServerAccess::isHttpRequest("POSTAL / HTTP/1.1\r\n\r\n"));
    REQUIRE(
        !HttpPollServerAccess::isHttpRequest("PATCHWORK / HTTP/1.1\r\n\r\n"));
    REQUIRE(!HttpPollServerAccess::isHttpRequest("GETTER / HTTP/1.1\r\n\r\n"));
    REQUIRE(!HttpPollServerAccess::isHttpRequest("PUTAWAY / HTTP/1.1\r\n\r\n"));
    REQUIRE(!HttpPollServerAccess::isHttpRequest("HEADING / HTTP/1.1\r\n\r\n"));
    REQUIRE(!HttpPollServerAccess::isHttpRequest("DELETED / HTTP/1.1\r\n\r\n"));
    REQUIRE(
        !HttpPollServerAccess::isHttpRequest("OPTIONSXYZ / HTTP/1.1\r\n\r\n"));
}

// ---------------------------------------------------------------------------
// 8. FG-5: isHttpRequest() accepts all seven supported methods
// ---------------------------------------------------------------------------
static void test_is_http_request_valid_methods() {
    BEGIN_TEST("isHttpRequest(): all supported methods accepted (FG-5)");
    REQUIRE(HttpPollServerAccess::isHttpRequest("GET / HTTP/1.1\r\n\r\n"));
    REQUIRE(HttpPollServerAccess::isHttpRequest("POST / HTTP/1.1\r\n\r\n"));
    REQUIRE(HttpPollServerAccess::isHttpRequest("PUT / HTTP/1.1\r\n\r\n"));
    REQUIRE(HttpPollServerAccess::isHttpRequest("HEAD / HTTP/1.1\r\n\r\n"));
    REQUIRE(HttpPollServerAccess::isHttpRequest("DELETE / HTTP/1.1\r\n\r\n"));
    REQUIRE(HttpPollServerAccess::isHttpRequest("OPTIONS / HTTP/1.1\r\n\r\n"));
    REQUIRE(HttpPollServerAccess::isHttpRequest("PATCH / HTTP/1.1\r\n\r\n"));
}

static void test_is_http_request_edge_cases() {
    BEGIN_TEST("isHttpRequest(): edge cases (FG-5)");
    // Empty string and short strings must not crash or return true.
    REQUIRE(!HttpPollServerAccess::isHttpRequest(""));
    REQUIRE(!HttpPollServerAccess::isHttpRequest("GET")); // no trailing space
    REQUIRE(!HttpPollServerAccess::isHttpRequest("UNKNOWN / HTTP/1.1\r\n\r\n"));
    // Method name without any path still needs the space.
    REQUIRE(!HttpPollServerAccess::isHttpRequest("GET"));
    REQUIRE(!HttpPollServerAccess::isHttpRequest("DELETE"));
}

// ---------------------------------------------------------------------------
int main() {
    // 1. parseHexSize_ overflow guard
    test_chunked_size_overflow();
    test_chunked_size_large_rejected();
    test_chunked_size_reasonable_accepted();

    // 2. FG-1: HttpRequest lifetime
    test_request_fields_outlive_source();
    test_request_query_params_outlive_source();

    // 3–4. FG-2: HttpResponse lifetime
    test_response_copy_survives_reset();
    test_response_copy_survives_parser_destruction();

    // 5–6. FG-4: HttpClientState move/copy semantics
    test_client_state_move_external_view();
    test_client_state_copy_external_view();
    test_client_state_move_internal_view();
    test_client_state_move_assign_internal_view();
    test_client_state_copy_internal_view();

    // 7–8. FG-5: isHttpRequest() disambiguation
    test_is_http_request_no_false_positives();
    test_is_http_request_valid_methods();
    test_is_http_request_edge_cases();

    return test_summary();
}
