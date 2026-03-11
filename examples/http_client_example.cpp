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
    printf("\n");

    // Example 4a: Redirect following — the easy way (default behaviour)
    //
    // HttpClient follows redirects automatically.  You just call get() and
    // get back the final response.  finalUrl and redirectChain are populated
    // for free if you want to inspect the path taken.
    printf("4a. Redirect following (automatic):\n");
    auto rrAuto = client.get("http://httpbin.org/redirect/2");
    if (rrAuto.isSuccess()) {
        const auto& resp = rrAuto.value();
        printf("   Final status: %d %s\n", resp.statusCode(),
            resp.statusText().data());
        printf("   Final URL:    %s\n", resp.finalUrl.c_str());
        printf("   Hops taken:   %zu\n", resp.redirectChain.size());
    } else {
        printf("   Error: %s\n", rrAuto.message().c_str());
    }

    printf("\n");

    // Example 4b: Redirect following — manual (advanced / diagnostic use only)
    //
    // Most users should NOT need this.  It is here to show what HttpClient
    // does internally when followRedirects = true.  Set followRedirects = false
    // only if you need to inspect or modify each hop yourself (e.g. rewriting
    // URLs, logging, auth token injection per-domain, etc.).
    printf("4b. Redirect following (manual, for illustration):\n");

    aiSocks::HttpClient::Options redirectOpts;
    redirectOpts.followRedirects = false;
    aiSocks::HttpClient redirectClient(redirectOpts);

    std::string nextUrl = "http://httpbin.org/redirect/2";
    int hop = 0;

    while (true) {
        printf("   [hop %d] GET %s\n", ++hop, nextUrl.c_str());
        auto redirectResponse = redirectClient.get(nextUrl);

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
            // resolveUrl() turns relative paths into absolute URLs — the same
            // helper HttpClient uses internally when followRedirects = true.
            nextUrl = aiSocks::HttpClient::resolveUrl(nextUrl, *loc);
            continue;
        }

        // Non-redirect: we're done.
        printf("   Final status: %d %s\n", resp.statusCode(),
            resp.statusText().data());
        printf("   Final URL: %s\n", nextUrl.c_str());
        break;
    }

    printf("\n=== HTTP Client Example Complete ===\n");
    return 0;
}
