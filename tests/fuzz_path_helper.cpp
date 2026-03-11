// Fuzz target for PathHelper path-traversal defence functions.
//
// PathHelper::normalizePath, getCanonicalPath, isPathWithin, joinPath,
// getFilename, and getExtension are the real security boundary — they
// determine whether a resolved path is still inside documentRoot.
// Any crash or out-of-bounds read here is a security bug.
//
// Two-input technique: the fuzzer input is split at the first '\0' byte
// into (childPath, parentPath) so isPathWithin gets realistic paired inputs.
//
// Build:  cmake --build build-fuzz --target fuzz_path_helper
// Run:    ./build-fuzz/tests/fuzz_path_helper fuzz_corpus/path_helper -max_len=1024

#include "PathHelper.h"
#include <cstddef>
#include <cstdint>
#include <string>

using namespace aiSocks;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // Split input at the first '\0' into two path strings.
    // If no '\0' present, use the whole buffer as childPath and "/" as parent.
    const uint8_t* sep = static_cast<const uint8_t*>(
        std::memchr(data, '\0', size));

    std::string child, parent;
    if (sep) {
        child  = std::string{reinterpret_cast<const char*>(data),
                             static_cast<size_t>(sep - data)};
        parent = std::string{reinterpret_cast<const char*>(sep + 1),
                             size - static_cast<size_t>(sep - data) - 1};
    } else {
        child  = std::string{reinterpret_cast<const char*>(data), size};
        parent = "/";
    }

    // Pure string-manipulation functions — no filesystem access.
    (void)PathHelper::normalizePath(child);
    (void)PathHelper::getCanonicalPath(child);
    (void)PathHelper::getFilename(child);
    (void)PathHelper::getExtension(child);
    (void)PathHelper::joinPath(parent, child);

    // The critical security check: does child stay within parent?
    (void)PathHelper::isPathWithin(child, parent);

    return 0;
}
