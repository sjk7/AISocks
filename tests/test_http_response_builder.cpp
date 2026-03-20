// This is an independent project of an individual developer. Dear PVS-Studio, please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
#include "HttpResponse.h"
#include "test_helpers.h"

using namespace aiSocks;

static void test_builder_simple() {
    BEGIN_TEST("HttpResponse::Builder simple");

    std::string resp = HttpResponse::builder()
        .status(200)
        .contentType("text/plain")
        .body("Hello World")
        .build();

    REQUIRE(resp.find("HTTP/1.1 200 OK\r\n") == 0);
    REQUIRE(resp.find("Content-Type: text/plain\r\n") != std::string::npos);
    REQUIRE(resp.find("Content-Length: 11\r\n") != std::string::npos);
    REQUIRE(resp.find("\r\n\r\nHello World") != std::string::npos);
}

static void test_builder_custom_reason() {
    BEGIN_TEST("HttpResponse::Builder custom reason");

    std::string resp = HttpResponse::builder()
        .status(404, "Not Found Custom")
        .build();

    REQUIRE(resp.find("HTTP/1.1 404 Not Found Custom\r\n") == 0);
}

static void test_builder_headers_and_close() {
    BEGIN_TEST("HttpResponse::Builder headers and close");

    std::string resp = HttpResponse::builder()
        .status(201)
        .header("X-Custom", "Value")
        .keepAlive(false)
        .body("Created")
        .build();

    REQUIRE(resp.find("HTTP/1.1 201 Created\r\n") == 0);
    REQUIRE(resp.find("X-Custom: Value\r\n") != std::string::npos);
    REQUIRE(resp.find("Connection: close\r\n") != std::string::npos);
}

int main() {
    g_totalTimer.reset();
    test_builder_simple();
    test_builder_custom_reason();
    test_builder_headers_and_close();
    return test_summary();
}
