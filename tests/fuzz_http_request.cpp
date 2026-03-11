// Fuzz target for HttpRequest::parse()
//
// Build (from repo root):
//   cmake -B build-fuzz -DENABLE_FUZZER=ON -DCMAKE_BUILD_TYPE=Debug
//   cmake --build build-fuzz --target fuzz_http_request
//
// Run (no corpus — starts from empty):
//   ./build-fuzz/tests/fuzz_http_request
//
// Run with a seed corpus directory:
//   mkdir -p fuzz_corpus/http_request
//   echo -n "GET / HTTP/1.1\r\n\r\n" > fuzz_corpus/http_request/seed1
//   ./build-fuzz/tests/fuzz_http_request fuzz_corpus/http_request

#include "HttpRequest.h"
#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // Feed raw bytes directly to the parser — it must survive any input.
    aiSocks::HttpRequest::parse({reinterpret_cast<const char*>(data), size});
    return 0;
}
