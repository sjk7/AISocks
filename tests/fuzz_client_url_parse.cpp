// Fuzz target for ClientHttpRequest::Builder::parseUrl() (internal URL parser).
//
// parseUrl() is called on every build() — it splits a URL into scheme, host,
// port, and path using string_view arithmetic.  Any bad arithmetic (OOB
// substr, npos mishandlings) will surface here.
//
// Build:  cmake --build build-fuzz --target fuzz_client_url_parse
// Run:    ./build-fuzz/tests/fuzz_client_url_parse -max_len=512

#include "ClientHttpRequest.h"
#include <cstddef>
#include <cstdint>
#include <string>

using namespace aiSocks;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // forUrl() calls parseUrl() internally then builds the request string.
    // We don't care about the output — we just must not crash or UB.
    std::string url{reinterpret_cast<const char*>(data), size};
    (void)ClientHttpRequest::forUrl(url);
    return 0;
}
