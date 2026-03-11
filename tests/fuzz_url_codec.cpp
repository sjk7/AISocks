// Fuzz target for urlDecode / urlDecodePath (UrlCodec.h).
//
// Exercises percent-decoding with arbitrary inputs: truncated %XX sequences,
// %00 null bytes, double-encoding (%252F), and every non-ASCII byte.
//
// Build:  cmake --build build-fuzz --target fuzz_url_codec
// Run:    ./build-fuzz/tests/fuzz_url_codec -max_len=1024

#include "UrlCodec.h"
#include <cstddef>
#include <cstdint>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    std::string_view sv{reinterpret_cast<const char*>(data), size};
    (void)aiSocks::urlDecode(sv);
    (void)aiSocks::urlDecodePath(sv);
    return 0;
}
