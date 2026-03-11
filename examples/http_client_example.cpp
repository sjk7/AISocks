// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, and Java:
// https://pvs-studio.com

// ---------------------------------------------------------------------------
// http_client_example.cpp -- Demonstration of HttpClient usage
//
// Requires C++17 for string_view and structured bindings
//
// Shows how to use the high-level HttpClient for common HTTP operations:
// - Simple GET requests
// - POST with JSON body
// - Custom headers
// - Redirect following
// ---------------------------------------------------------------------------

#include "HttpClient.h"
#include <iostream>
#include <string>

int main() {
    printf("=== HTTP Client Example ===\n\n");

    aiSocks::HttpClient client;

    // Example 1: Simple GET request
    printf("1. Simple GET request:\n");
    auto response = client.get("http://httpbin.org/get");
    if (response.isSuccess()) {
        const auto& resp = response.value();
        printf(
            "   Status: %d %s\n", resp.statusCode(), resp.statusText().data());
        printf("   Content-Type: %s\n", resp.contentType().data());
        printf("   Body size: %zu bytes\n", resp.body().size());

        // Print first 200 characters of body
        if (resp.body().size() > 0) {
            size_t toPrint = std::min(resp.body().size(), size_t{200});
            printf("   Body preview: %.*s%s\n", static_cast<int>(toPrint),
                resp.body().data(), resp.body().size() > 200 ? "..." : "");
        }
    } else {
        printf("   Error: %s\n", response.message().c_str());
    }

    printf("\n");

    // Example 2: POST request with JSON
    printf("2. POST request with JSON:\n");
    std::string jsonBody = R"({"name": "John", "age": 30})";
    auto postResponse
        = client.post("http://httpbin.org/post", jsonBody, "application/json");
    if (postResponse.isSuccess()) {
        const auto& resp = postResponse.value();
        printf(
            "   Status: %d %s\n", resp.statusCode(), resp.statusText().data());
        printf("   Content-Type: %s\n", resp.contentType().data());

        // Look for our JSON in the response
        if (resp.body().find("John") != std::string_view::npos) {
            printf("   ✓ JSON data echoed back correctly\n");
        }
    } else {
        printf("   Error: %s\n", postResponse.message().c_str());
    }

    printf("\n");

    // Example 3: Custom headers
    printf("3. Request with custom headers:\n");
    aiSocks::HttpClient::Options options;
    options.setHeader("X-Custom-Header", "test-value")
        .setHeader("User-Agent", "AISocks-Example/1.0");

    aiSocks::HttpClient customClient(options);
    auto headerResponse = customClient.get("http://httpbin.org/headers");
    if (headerResponse.isSuccess()) {
        const auto& resp = headerResponse.value();
        printf(
            "   Status: %d %s\n", resp.statusCode(), resp.statusText().data());

        if (resp.body().find("test-value") != std::string_view::npos) {
            printf("   ✓ Custom header sent successfully\n");
        }
        if (resp.body().find("AISocks-Example") != std::string_view::npos) {
            printf("   ✓ Custom User-Agent sent successfully\n");
        }
    } else {
        printf("   Error: %s\n", headerResponse.message().c_str());
    }

    printf("\n");

    // Example 4: Redirect following (manual, so each get() call visibly uses
    // the Location URL returned by the previous socket).
    //
    // followRedirects = false means the 3xx response is returned as-is.
    // We read the Location header, close the old connection, and pass that
    // URL directly to the next get() call — showing the socket result drives
    // the next request.
    printf("4. Redirect following:\n");

    aiSocks::HttpClient::Options redirectOpts;
    redirectOpts.followRedirects = false;
    aiSocks::HttpClient redirectClient(redirectOpts);

    std::string nextUrl = "http://httpbin.org/redirect/2";
    int hop = 0;
    aiSocks::Result<aiSocks::HttpClientResponse> redirectResponse
        = aiSocks::Result<aiSocks::HttpClientResponse>::failure(
            aiSocks::SocketError::Unknown, "not started");

    while (true) {
        printf("   [hop %d] GET %s\n", ++hop, nextUrl.c_str());
        redirectResponse = redirectClient.get(nextUrl);

        if (!redirectResponse.isSuccess()) {
            printf("   Error: %s\n", redirectResponse.message().c_str());
            break;
        }

        const auto& resp = redirectResponse.value();
        if (resp.isRedirect()) {
            const std::string* loc = resp.header("location");
            if (!loc) {
                printf("   Error: redirect without Location header\n");
                break;
            }
            printf("   <- %d %s  (socket closed)\n", resp.statusCode(),
                resp.statusText().data());
            printf("      Location: %s\n", loc->c_str());
            nextUrl = *loc; // URL from the socket result drives the next get()
            continue;
        }

        // Non-redirect: we're done.
        break;
    }

    if (redirectResponse.isSuccess() && redirectResponse.value().isSuccess()) {
        const auto& resp = redirectResponse.value();
        printf("   Final status: %d %s\n", resp.statusCode(),
            resp.statusText().data());
        printf("   Final URL: %s\n", nextUrl.c_str());
    }

    printf("\n=== HTTP Client Example Complete ===\n");
    return 0;
}
