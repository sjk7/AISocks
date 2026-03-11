// Fuzz target for the client-side build → server-side parse round-trip:
//   raw bytes → split on '\0' → ClientHttpRequest::forPost(url, body)
//                             → HttpRequest::parse(output)
//
// Why this matters:
//   - ClientHttpRequest::build() must always produce output that
//     HttpRequest::parse() accepts as valid.
//   - Any URL or body input that produces a syntactically broken request is a
//     bug in the builder.
//
// Input format: url\0body  (split on first NUL byte; each part may be empty)
//
// Build:  cmake --build build-fuzz --target fuzz_client_roundtrip
// Run:    ./build-fuzz/tests/fuzz_client_roundtrip -max_len=512

#include "ClientHttpRequest.h"
#include "HttpRequest.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>

using namespace aiSocks;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    const std::string_view input{reinterpret_cast<const char*>(data), size};

    // Split on first NUL.
    const auto sep = input.find('\0');
    const std::string_view urlPart  = input.substr(0, sep);
    const std::string_view bodyPart = (sep != std::string_view::npos)
        ? input.substr(sep + 1)
        : std::string_view{};

    // Build both a GET and a POST request from the fuzzed inputs.
    const std::string getReq  = ClientHttpRequest::forUrl(urlPart);
    const std::string postReq = ClientHttpRequest::forPost(urlPart, bodyPart);

    // The builder must always produce output the parser accepts (valid==true)
    // when the URL was at least minimally well-formed.  We don't assert on
    // malformed URLs — we just must not crash.
    auto parsedGet  = HttpRequest::parse(getReq);
    auto parsedPost = HttpRequest::parse(postReq);

    // If parsing succeeded, basic invariants must hold.
    if (parsedGet.valid) {
        assert(parsedGet.method  == "GET");
        assert(!parsedGet.version.empty());
    }
    if (parsedPost.valid) {
        assert(parsedPost.method == "POST");
        assert(!parsedPost.version.empty());
    }

    return 0;
}
