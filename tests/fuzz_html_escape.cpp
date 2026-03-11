// Fuzz target for HtmlEscape::encode (HtmlEscape.h).
//
// Checks that the XSS-escaping function handles every possible byte sequence
// without crashing or reading out-of-bounds.
//
// Build:  cmake --build build-fuzz --target fuzz_html_escape
// Run:    ./build-fuzz/tests/fuzz_html_escape -max_len=1024

#include "HtmlEscape.h"
#include <cstddef>
#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    std::string input{reinterpret_cast<const char*>(data), size};
    (void)aiSocks::HtmlEscape::encode(input);
    return 0;
}
