// This is an independent project of an individual developer. Dear PVS-Studio, please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com

// Tests for HttpResponseParser (feed-based incremental parser).
//
// Coverage goals:
// 1.  Simple 200 OK with Content-Length body
// 2.  Single-chunk feed (whole response at once)
// 3.  Multi-chunk feed (byte-by-byte confirms incremental correctness)
// 4.  Chunked Transfer-Encoding
// 5.  204 No Content (zero-length body)
// 6.  Header field case-folding and OWS trimming
// 7.  feedEof() for Connection-Close responses
// 8.  Bare LF (\n\n) as header/body separator -- RFC 7230 §3.5
// 9.  Bare LF (\n) as line terminator throughout -- RFC 7230 §3.5
// 10. Malformed status line -> Error state
// 11. reset() between keep-alive responses
// 12. isHeadersComplete() fires before body is done

#include "HttpResponse.h"
#include "test_helpers.h"

#include <string>
#include <string_view>

using namespace aiSocks;

static void CHECK_FIELD(
    const std::string& label, std::string_view got, std::string_view expected) {
    REQUIRE_MSG(got == expected,
        label + ": expected \"" + std::string(expected) + "\"  got \""
            + std::string(got) + "\"");
}

// ---------------------------------------------------------------------------
// 1. Simple 200 OK with Content-Length
// ---------------------------------------------------------------------------
static void test_simple_200() {
    BEGIN_TEST("simple 200 OK");
    std::string raw = "HTTP/1.1 200 OK\r\n"
                      "Content-Length: 5\r\n"
                      "Content-Type: text/plain\r\n"
                      "\r\n"
                      "Hello";
    HttpResponseParser p;
    auto state = p.feed(raw.data(), raw.size());
    REQUIRE(state == HttpResponseParser::State::Complete);
    REQUIRE(p.response().valid); //-V807
    REQUIRE(p.response().statusCode == 200);
    CHECK_FIELD("version", p.response().version(), "HTTP/1.1");
    CHECK_FIELD("statusText", p.response().statusText(), "OK");
    CHECK_FIELD("body", p.response().body(), "Hello");
    CHECK_FIELD(
        "content-type", *p.response().header("content-type"), "text/plain");
}

// ---------------------------------------------------------------------------
// 2. Multi-chunk feed
// ---------------------------------------------------------------------------
static void test_incremental_feed() {
    BEGIN_TEST("incremental feed");
    std::string raw = "HTTP/1.1 200 OK\r\n"
                      "Content-Length: 3\r\n"
                      "\r\n"
                      "ABC";
    HttpResponseParser p;
    HttpResponseParser::State state = HttpResponseParser::State::Incomplete;
    for (char c : raw) state = p.feed(&c, 1);
    REQUIRE(state == HttpResponseParser::State::Complete);
    CHECK_FIELD("body", p.response().body(), "ABC");
}

// ---------------------------------------------------------------------------
// 3. Chunked Transfer-Encoding
// ---------------------------------------------------------------------------
static void test_chunked() {
    BEGIN_TEST("chunked TE");
    std::string raw = "HTTP/1.1 200 OK\r\n"
                      "Transfer-Encoding: chunked\r\n"
                      "\r\n"
                      "5\r\n"
                      "Hello\r\n"
                      "6\r\n"
                      " World\r\n"
                      "0\r\n"
                      "\r\n";
    HttpResponseParser p;
    auto state = p.feed(raw.data(), raw.size());
    REQUIRE(state == HttpResponseParser::State::Complete);
    CHECK_FIELD("chunked body", p.response().body(), "Hello World");
}

// ---------------------------------------------------------------------------
// 4. 204 No Content
// ---------------------------------------------------------------------------
static void test_204_no_content() {
    BEGIN_TEST("204 No Content");
    std::string raw = "HTTP/1.1 204 No Content\r\n"
                      "Content-Length: 0\r\n"
                      "\r\n";
    HttpResponseParser p;
    auto state = p.feed(raw.data(), raw.size());
    REQUIRE(state == HttpResponseParser::State::Complete);
    REQUIRE(p.response().statusCode == 204);
    REQUIRE(p.response().body().empty());
}

// ---------------------------------------------------------------------------
// 5. Header OWS trimming and case folding
// ---------------------------------------------------------------------------
static void test_header_ows_and_case() {
    BEGIN_TEST("header OWS and case folding");
    std::string raw = "HTTP/1.1 200 OK\r\n"
                      "Content-Length: 0\r\n"
                      "X-CUSTOM:   trimmed   \r\n"
                      "\r\n";
    HttpResponseParser p;
    p.feed(raw.data(), raw.size());
    REQUIRE(p.isComplete());
    CHECK_FIELD("x-custom", *p.response().header("x-custom"), "trimmed");
    CHECK_FIELD("X-CUSTOM alias", *p.response().header("X-CUSTOM"), "trimmed");
}

// ---------------------------------------------------------------------------
// 6. feedEof() for Connection-Close response
// ---------------------------------------------------------------------------
static void test_feed_eof() {
    BEGIN_TEST("feedEof() connection-close");
    std::string raw = "HTTP/1.1 200 OK\r\n"
                      "\r\n"
                      "body text";
    HttpResponseParser p;
    auto state = p.feed(raw.data(), raw.size());
    REQUIRE(state == HttpResponseParser::State::HeadersComplete
        || state == HttpResponseParser::State::Incomplete);
    state = p.feedEof();
    REQUIRE(state == HttpResponseParser::State::Complete);
    CHECK_FIELD("eof body", p.response().body(), "body text");
}

// ---------------------------------------------------------------------------
// 7. Bare LF (\n\n) as separator -- RFC 7230 §3.5
// ---------------------------------------------------------------------------
static void test_bare_lf_separator() {
    BEGIN_TEST("bare LF separator (\\n\\n)");
    std::string raw = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\nhi";
    HttpResponseParser p;
    auto state = p.feed(raw.data(), raw.size());
    REQUIRE(state == HttpResponseParser::State::Complete);
    REQUIRE(p.response().statusCode == 200);
    CHECK_FIELD("body", p.response().body(), "hi");
}

// ---------------------------------------------------------------------------
// 8. Bare LF throughout
// ---------------------------------------------------------------------------
static void test_bare_lf_line_endings() {
    BEGIN_TEST("bare LF line endings");
    std::string raw = "HTTP/1.1 200 OK\n"
                      "Content-Length: 5\n"
                      "X-Foo: bar\n"
                      "\n"
                      "World";
    HttpResponseParser p;
    auto state = p.feed(raw.data(), raw.size());
    REQUIRE(state == HttpResponseParser::State::Complete);
    const auto& resp = p.response();
    REQUIRE(resp.statusCode == 200);
    CHECK_FIELD("x-foo", *resp.header("x-foo"), "bar");
    CHECK_FIELD("body", resp.body(), "World");
}

// ---------------------------------------------------------------------------
// 9. Malformed status line -> Error
// ---------------------------------------------------------------------------
static void test_malformed_status_line() {
    BEGIN_TEST("malformed status line");
    std::string raw = "NOTHTTP\r\n\r\n";
    HttpResponseParser p;
    auto state = p.feed(raw.data(), raw.size());
    REQUIRE(state == HttpResponseParser::State::Error);
    REQUIRE(p.isError());
}

// 9b. Invalid HTTP version token with valid code is rejected
static void test_reject_invalid_http_version_token() {
    BEGIN_TEST("reject invalid HTTP version token");
    std::string raw = "XYZ 200 OK\r\n\r\n";
    HttpResponseParser p;
    auto state = p.feed(raw.data(), raw.size());
    REQUIRE(state == HttpResponseParser::State::Error);
    REQUIRE(p.isError());
}

// 9c. Only HTTP/1.0 and HTTP/1.1 are accepted
static void test_reject_unsupported_http_versions() {
    BEGIN_TEST("reject unsupported HTTP version values");
    {
        std::string raw = "HTTP/2 200 OK\r\n\r\n";
        HttpResponseParser p;
        auto state = p.feed(raw.data(), raw.size());
        REQUIRE(state == HttpResponseParser::State::Error);
        REQUIRE(p.isError());
    }
    {
        std::string raw = "HTTP/1.2 200 OK\r\n\r\n";
        HttpResponseParser p;
        auto state = p.feed(raw.data(), raw.size());
        REQUIRE(state == HttpResponseParser::State::Error);
        REQUIRE(p.isError());
    }
}

// ---------------------------------------------------------------------------
// 10. reset() between keep-alive responses
// ---------------------------------------------------------------------------
static void test_reset_keepalive() {
    BEGIN_TEST("reset() keep-alive");
    std::string r1 = "HTTP/1.1 200 OK\r\n"
                     "Content-Length: 1\r\n"
                     "\r\n"
                     "A";
    std::string r2 = "HTTP/1.1 201 Created\r\n"
                     "Content-Length: 1\r\n"
                     "\r\n"
                     "B";
    HttpResponseParser p;
    p.feed(r1.data(), r1.size());
    REQUIRE(p.isComplete());
    REQUIRE(p.response().statusCode == 200);

    p.reset();
    REQUIRE(!p.isComplete());

    p.feed(r2.data(), r2.size());
    REQUIRE(p.isComplete());
    const auto& resp2 = p.response();
    REQUIRE(resp2.statusCode == 201);
    CHECK_FIELD("second body", p.response().body(), "B");
}

// ---------------------------------------------------------------------------
// 11. isHeadersComplete() fires early
// ---------------------------------------------------------------------------
static void test_headers_complete_early() {
    BEGIN_TEST("isHeadersComplete() before body");
    // Feed only headers + separator, no body yet.
    std::string headers = "HTTP/1.1 200 OK\r\n"
                          "Content-Length: 10\r\n"
                          "\r\n";
    HttpResponseParser p;
    auto state = p.feed(headers.data(), headers.size());
    REQUIRE(state == HttpResponseParser::State::HeadersComplete
        || state == HttpResponseParser::State::Incomplete);
    REQUIRE(p.isHeadersComplete());
    REQUIRE(!p.isComplete());
    REQUIRE(p.response().statusCode == 200);
}

// 12. Oversized header section is rejected
static void test_reject_oversized_headers() {
    BEGIN_TEST("reject oversized response headers");
    std::string huge(70000, 'h');
    std::string raw = "HTTP/1.1 200 OK\r\nX-Huge: " + huge + "\r\n\r\nbody";
    HttpResponseParser p;
    auto state = p.feed(raw.data(), raw.size());
    REQUIRE(state == HttpResponseParser::State::Error);
    REQUIRE(p.isError());
}

// 13. Content-Length beyond configured cap is rejected
static void test_reject_oversized_content_length() {
    BEGIN_TEST("reject oversized Content-Length");
    std::string raw = "HTTP/1.1 200 OK\r\n"
                      "Content-Length: 67108865\r\n"
                      "\r\n";
    HttpResponseParser p;
    auto state = p.feed(raw.data(), raw.size());
    REQUIRE(state == HttpResponseParser::State::Error);
    REQUIRE(p.isError());
}

// 14. Chunk size beyond cap is rejected without needing full body bytes
static void test_reject_oversized_chunk_size_line() {
    BEGIN_TEST("reject oversized chunk size line");
    std::string raw = "HTTP/1.1 200 OK\r\n"
                      "Transfer-Encoding: chunked\r\n"
                      "\r\n"
                      "1000001\r\n";
    HttpResponseParser p;
    auto state = p.feed(raw.data(), raw.size());
    REQUIRE(state == HttpResponseParser::State::Error);
    REQUIRE(p.isError());
}

// 15. Content-Length at configured cap is accepted (headers-only phase)
static void test_accept_max_content_length_header() {
    BEGIN_TEST("accept max Content-Length header");
    std::string raw = "HTTP/1.1 200 OK\r\n"
                      "Content-Length: 67108864\r\n"
                      "\r\n";
    HttpResponseParser p;
    auto state = p.feed(raw.data(), raw.size());
    REQUIRE(state == HttpResponseParser::State::HeadersComplete
        || state == HttpResponseParser::State::Incomplete);
    REQUIRE(!p.isError());
}

// 16. Chunk size line at configured cap is accepted (awaiting body bytes)
static void test_accept_max_chunk_size_line() {
    BEGIN_TEST("accept max chunk size line");
    std::string raw = "HTTP/1.1 200 OK\r\n"
                      "Transfer-Encoding: chunked\r\n"
                      "\r\n"
                      "1000000\r\n";
    HttpResponseParser p;
    auto state = p.feed(raw.data(), raw.size());
    REQUIRE(state != HttpResponseParser::State::Error);
    REQUIRE(!p.isError());
}

// 17. Decoded chunked body beyond configured cap is rejected
static void test_reject_decoded_chunked_body_over_cap() {
    BEGIN_TEST("reject decoded chunked body beyond cap");
    HttpResponseParser p;

    const std::string headers = "HTTP/1.1 200 OK\r\n"
                                "Transfer-Encoding: chunked\r\n"
                                "\r\n";
    auto state = p.feed(headers.data(), headers.size());
    REQUIRE(state != HttpResponseParser::State::Error);

    const std::string chunkPayload(HttpResponseParser::kMaxChunkSize, 'a');
    const std::string chunk = "1000000\r\n" + chunkPayload + "\r\n";

    for (int i = 0; i < 4; ++i) {
        state = p.feed(chunk.data(), chunk.size());
        REQUIRE(state != HttpResponseParser::State::Error);
    }

    const std::string extraChunk = "1\r\nZ\r\n";
    state = p.feed(extraChunk.data(), extraChunk.size());
    REQUIRE(state == HttpResponseParser::State::Error);
    REQUIRE(p.isError());
}

// 18. Overflow-formatted chunk sizes are rejected cleanly
static void test_reject_overflow_formatted_chunk_size() {
    BEGIN_TEST("reject overflow-formatted chunk size");
    std::string raw = "HTTP/1.1 200 OK\r\n"
                      "Transfer-Encoding: chunked\r\n"
                      "\r\n"
                      "FFFFFFFFFFFFFFFF\r\n";
    HttpResponseParser p;
    auto state = p.feed(raw.data(), raw.size());
    REQUIRE(state == HttpResponseParser::State::Error);
    REQUIRE(p.isError());
}

// ---------------------------------------------------------------------------
int main() {
    test_simple_200();
    test_incremental_feed();
    test_chunked();
    test_204_no_content();
    test_header_ows_and_case();
    test_feed_eof();
    test_bare_lf_separator();
    test_bare_lf_line_endings();
    test_malformed_status_line();
    test_reject_invalid_http_version_token();
    test_reject_unsupported_http_versions();
    test_reset_keepalive();
    test_headers_complete_early();
    test_reject_oversized_headers();
    test_reject_oversized_content_length();
    test_reject_oversized_chunk_size_line();
    test_accept_max_content_length_header();
    test_accept_max_chunk_size_line();
    test_reject_decoded_chunked_body_over_cap();
    test_reject_overflow_formatted_chunk_size();
    return test_summary();
}
