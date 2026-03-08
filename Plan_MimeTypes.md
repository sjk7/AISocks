# Plan: Extract MimeTypes lookup from HttpFileServer

## What & Why

`HttpFileServer::getMimeType()` is a static lookup table with 24 entries that maps
file extensions to MIME type strings. It has no dependency on any instance state —
it is a pure function. Living inside `HttpFileServer` means: adding a new MIME type
requires editing the file server, and the table cannot easily be reused by other
code.

The fix: move it to a `namespace MimeTypes` with a single `inline` free function
in a new header. `HttpFileServer` delegates to it.

---

## Relevant Files

- `lib/include/MimeTypes.h` — **new file** (header-only)
- `lib/include/HttpFileServer.h` — remove `getMimeType()` declaration; add include
- `lib/src/HttpFileServer.cpp` — remove definition; update call sites

---

## Step 1 — Create `lib/include/MimeTypes.h`

```cpp
#ifndef AISOCKS_MIME_TYPES_H
#define AISOCKS_MIME_TYPES_H

#include <map>
#include <string>

namespace aiSocks {
namespace MimeTypes {

// Returns the MIME type for the given file path based on its extension.
// Falls back to "application/octet-stream" for unknown extensions.
inline std::string fromPath(const std::string& filePath) {
    static const std::map<std::string, std::string> table = {
        {".html",  "text/html"},
        {".htm",   "text/html"},
        {".css",   "text/css"},
        {".js",    "application/javascript"},
        {".json",  "application/json"},
        {".xml",   "application/xml"},
        {".txt",   "text/plain"},
        {".md",    "text/markdown"},
        {".png",   "image/png"},
        {".jpg",   "image/jpeg"},
        {".jpeg",  "image/jpeg"},
        {".gif",   "image/gif"},
        {".svg",   "image/svg+xml"},
        {".ico",   "image/x-icon"},
        {".pdf",   "application/pdf"},
        {".zip",   "application/zip"},
        {".gz",    "application/gzip"},
        {".mp3",   "audio/mpeg"},
        {".mp4",   "video/mp4"},
        {".webm",  "video/webm"},
        {".woff",  "font/woff"},
        {".woff2", "font/woff2"},
        {".ttf",   "font/ttf"},
        {".eot",   "application/vnd.ms-fontobject"},
    };

    // Extract extension — last '.' to end, e.g. "/foo/bar.HTML" -> ".HTML"
    const size_t dot = filePath.rfind('.');
    if (dot == std::string::npos || dot + 1 == filePath.size())
        return "application/octet-stream";

    const std::string ext = filePath.substr(dot);

    // Case-insensitive lookup: convert extension to lowercase.
    std::string extLower;
    extLower.reserve(ext.size());
    for (char c : ext)
        extLower += static_cast<char>(
            (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c);

    auto it = table.find(extLower);
    return it != table.end() ? it->second : "application/octet-stream";
}

} // namespace MimeTypes
} // namespace aiSocks

#endif // AISOCKS_MIME_TYPES_H
```

> **Note:** The new version adds lowercase normalisation. The original
> `getMimeType()` called `getFileExtension()` which returns the extension as-is
> from the path (no lowercasing), so `.HTML` would miss the table. The new version
> fixes that silently. If exact backward compatibility is required, remove the
> lowercase transform.

---

## Step 2 — Update `lib/include/HttpFileServer.h`

Add include:
```cpp
#include "MimeTypes.h"
```

Remove declaration:
```cpp
std::string getMimeType(const std::string& filePath) const;
```

---

## Step 3 — Update `lib/src/HttpFileServer.cpp`

Remove the `getMimeType()` definition (the `static const std::map` block, ~30 lines).

Replace all call sites — there are three in `handleFileRequest()` and two in
`sendCachedFile()`:

```cpp
// Before:
getMimeType(filePath)

// After:
MimeTypes::fromPath(filePath)
```

Also: since `getMimeType()` was the only caller of `getFileExtension()`, check
whether `getFileExtension()` is called anywhere else:
```bash
grep -n "getFileExtension" lib/src/HttpFileServer.cpp lib/include/HttpFileServer.h
```
If not, remove its declaration and definition too (they are replaced by the
`rfind('.')` inside `MimeTypes::fromPath`).

---

## Verification

```bash
# 1. Confirm no remaining references
grep -rn "getMimeType\|getFileExtension" lib/

# 2. Build
cmake --build build_Mac_arm --config Debug 2>&1 | tail -10

# 3. Tests
cd build_Mac_arm && ctest --output-on-failure -j4
# expect: 100% pass, 30/30

# 4. Smoke-check: request a .js and a .png and confirm correct Content-Type
curl -I http://localhost:<port>/index.html
curl -I http://localhost:<port>/style.css
```

---

## Scope / Constraints

- `MimeTypes.h` is header-only — no CMakeLists change needed.
- The `static const map` is initialised once (first call) and reused — identical
  runtime behaviour to the original.
- `getFileExtension()` is an internal helper with no public callers; it is safe to
  remove once `getMimeType()` is gone.
- The case-normalisation change is a minor bug-fix, not a behaviour regression.
  If the project has tests that assert on extension case sensitivity, update them.
