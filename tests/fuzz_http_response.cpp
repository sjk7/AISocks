// Fuzz target for HttpResponseParser (incremental feed-based parser).
//
// Build:  cmake --build build-fuzz --target fuzz_http_response
// Run:    ./build-fuzz/tests/fuzz_http_response -max_len=1024

#include "HttpResponse.h"
#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    aiSocks::HttpResponseParser parser;
    // Feed the entire blob in one shot, then signal EOF.
    // The parser must survive any byte sequence without crashing.
    parser.feed(reinterpret_cast<const char*>(data), size);
    parser.feedEof();
    return 0;
}
