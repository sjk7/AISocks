# Plan: Extract FileServerUtils from HttpFileServer

## What & Why

`HttpFileServer` contains four utility functions that have no dependency on the
server's socket state, client map, or configuration beyond one boolean flag:

| Method | What it does | SRP concern |
|---|---|---|
| `urlDecodePath()` | `%xx`-decodes a URL path segment | pure string utility |
| `getFileExtension()` | Returns the `.ext` suffix of a path | pure string utility |
| `formatHttpDate()` | Formats a `time_t` as RFC 7231 HTTP-date | pure date utility |
| `addSecurityHeaders()` | Appends 4 security headers to a response | writes to response; reads one config flag |

All four can become free functions. The server's `HttpFileServer` calls them with
simple values — no `this` state required beyond `config_.enableSecurityHeaders`,
which can be passed as a `bool` argument.

Benefit: new callers (tests, other servers) can use HTTP-date formatting or URL
decoding without instantiating `HttpFileServer`.

---

## Relevant Files

- `lib/include/FileServerUtils.h` — **new file** (header-only)
- `lib/include/HttpFileServer.h` — remove four method declarations; add include
- `lib/src/HttpFileServer.cpp` — remove four method definitions; update call sites

---

## Step 1 — Create `lib/include/FileServerUtils.h`

```cpp
#ifndef AISOCKS_FILE_SERVER_UTILS_H
#define AISOCKS_FILE_SERVER_UTILS_H

#include "StringBuilder.h"
#include <array>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <string>

#ifdef _WIN32
#include <time.h> // gmtime_s
#endif

namespace aiSocks {
namespace FileServerUtils {

// ---------------------------------------------------------------------------
// urlDecodePath
//
// Decodes %xx percent-encoding in a URL path.  Non-hex-pair occurrences of '%'
// are passed through unchanged so partial / malformed sequences don't corrupt
// the output.
//
// Uses a static 256-entry lookup table built once at first call so repeated
// calls pay no initialisation cost.
// ---------------------------------------------------------------------------
inline std::string urlDecodePath(const std::string& src) {
    static const auto fromHex = []() noexcept {
        std::array<uint8_t, 256> t{};
        t.fill(0xFF);
        for (int i = 0; i < 10; ++i)
            t[static_cast<unsigned>('0' + i)] = static_cast<uint8_t>(i);
        for (int i = 0; i < 6; ++i) {
            t[static_cast<unsigned>('A' + i)] = static_cast<uint8_t>(10 + i);
            t[static_cast<unsigned>('a' + i)] = static_cast<uint8_t>(10 + i);
        }
        return t;
    }();

    std::string out;
    out.reserve(src.size());
    for (size_t i = 0, n = src.size(); i < n; ++i) {
        const unsigned char c = static_cast<unsigned char>(src[i]);
        if (c == '%' && i + 2 < n) {
            const uint8_t hi = fromHex[static_cast<unsigned char>(src[i + 1])];
            const uint8_t lo = fromHex[static_cast<unsigned char>(src[i + 2])];
            if (hi != 0xFF && lo != 0xFF) {
                out += static_cast<char>((hi << 4) | lo);
                i += 2;
                continue;
            }
        }
        out += static_cast<char>(c);
    }
    return out;
}

// ---------------------------------------------------------------------------
// getFileExtension
//
// Returns the last dot-prefixed suffix of filePath (e.g. ".html"),
// or an empty string if there is no extension.
// ---------------------------------------------------------------------------
inline std::string getFileExtension(const std::string& filePath) {
    const size_t dotPos = filePath.find_last_of('.');
    if (dotPos != std::string::npos && dotPos < filePath.length() - 1)
        return filePath.substr(dotPos);
    return {};
}

// ---------------------------------------------------------------------------
// formatHttpDate
//
// Formats fileTime as "Day, DD Mon YYYY HH:MM:SS GMT"
// (the RFC 7231 / HTTP-date preferred format).
//
// Uses gmtime_s on Windows (re-entrant) and gmtime elsewhere.
// ---------------------------------------------------------------------------
inline std::string formatHttpDate(time_t fileTime) {
    char buffer[32];
#ifdef _WIN32
    struct tm timeinfo = {};
    gmtime_s(&timeinfo, &fileTime);
    strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S GMT", &timeinfo);
#else
    struct tm* timeinfo = gmtime(&fileTime);
    strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S GMT", timeinfo);
#endif
    return std::string(buffer);
}

// ---------------------------------------------------------------------------
// addSecurityHeaders
//
// Appends four HTTP security response headers to `response` when
// `enabled` is true.  The headers are:
//   - X-Content-Type-Options: nosniff (prevents MIME-sniffing attacks)
//   - X-Frame-Options: DENY (blocks framing by any origin)
//   - Content-Security-Policy: default-src 'self' ... (restricts resource loads)
//   - Referrer-Policy: no-referrer (omits Referer header on outbound navigations)
// ---------------------------------------------------------------------------
inline void addSecurityHeaders(StringBuilder& response, bool enabled) {
    if (!enabled) return;
    response.append("X-Content-Type-Options: nosniff\r\n");
    response.append("X-Frame-Options: DENY\r\n");
    response.append(
        "Content-Security-Policy: default-src 'self'; style-src 'self' "
        "'unsafe-inline'; script-src 'self' 'unsafe-inline'\r\n");
    response.append("Referrer-Policy: no-referrer\r\n");
}

} // namespace FileServerUtils
} // namespace aiSocks

#endif // AISOCKS_FILE_SERVER_UTILS_H
```

---

## Step 2 — Update `lib/include/HttpFileServer.h`

### Add include

```cpp
#include "FileServerUtils.h"
```

### Remove four method declarations

```cpp
// Remove these:
std::string urlDecodePath(const std::string& src);
std::string getFileExtension(const std::string& filePath) const;
std::string formatHttpDate(time_t fileTime) const;
void addSecurityHeaders(StringBuilder& response) const;
```

---

## Step 3 — Update `lib/src/HttpFileServer.cpp`

### Remove four method definitions

Delete the bodies of:
- `HttpFileServer::urlDecodePath(...)` (~30 lines, from `static const auto fromHex` through the loop)
- `HttpFileServer::getFileExtension(...)` (~7 lines)
- `HttpFileServer::formatHttpDate(...)` (~10 lines)
- `HttpFileServer::addSecurityHeaders(...)` (~8 lines)

### Update call sites (sed-friendly one-liners shown)

| Old call | New call |
|---|---|
| `urlDecodePath(path)` | `FileServerUtils::urlDecodePath(path)` |
| `getFileExtension(filePath)` | `FileServerUtils::getFileExtension(filePath)` |
| `formatHttpDate(fileInfo.lastModified)` | `FileServerUtils::formatHttpDate(fileInfo.lastModified)` |
| `addSecurityHeaders(response)` | `FileServerUtils::addSecurityHeaders(response, config_.enableSecurityHeaders)` |

Call sites (from grep):
```
lib/src/HttpFileServer.cpp:105   urlDecodePath(path)
lib/src/HttpFileServer.cpp:148   getFileExtension(filePath)
lib/src/HttpFileServer.cpp:286   formatHttpDate(fileInfo.lastModified)
lib/src/HttpFileServer.cpp:318   formatHttpDate(fileInfo.lastModified)
lib/src/HttpFileServer.cpp:328   addSecurityHeaders(response)
lib/src/HttpFileServer.cpp:360   addSecurityHeaders(response)
lib/src/HttpFileServer.cpp:383   formatHttpDate(fileInfo.lastModified)
lib/src/HttpFileServer.cpp:393   addSecurityHeaders(response)
lib/src/HttpFileServer.cpp:427   formatHttpDate(fileInfo.lastModified)
lib/src/HttpFileServer.cpp:437   addSecurityHeaders(response)
lib/src/HttpFileServer.cpp:464   addSecurityHeaders(response)
```

---

## Verification

```bash
# 1. Build
cmake --build build_Mac_arm --config Debug 2>&1 | tail -10

# 2. Tests
cd build_Mac_arm && ctest --output-on-failure -j4
# expect: 100% pass, 30/30

# 3. Smoke — URL with percent-encoded path
curl -v "http://localhost:<port>/test%20file.txt"
# expect: correct path resolution (spaces decoded)

# 4. Smoke — security headers present in response
curl -I "http://localhost:<port>/any-file"
# expect: X-Content-Type-Options, X-Frame-Options, CSP, Referrer-Policy headers

# 5. Smoke — Last-Modified header format
curl -I "http://localhost:<port>/any-file"
# expect: Last-Modified: Fri, 07 Mar 2025 12:34:56 GMT  (RFC 7231 format)
```

---

## Scope / Constraints

- Header-only — no CMakeLists change needed.
- `getFileExtension` is also used internally by `getMimeType()` (see Plan_MimeTypes.md).
  After `Plan_MimeTypes.md` is applied, `getMimeType()` calls `MimeTypes::fromPath()`
  which will contain its own extension extraction; `FileServerUtils::getFileExtension`
  may then become unused and can be removed. Until then keep it.
- `addSecurityHeaders` now takes `bool enabled` instead of reading `config_` directly.
  All call sites pass `config_.enableSecurityHeaders` — this makes the function
  testable without a `Config` object.
- If `Plan_HtmlEscape.md` has not yet been applied, `htmlEscape()` in HttpFileServer.cpp
  is still present and unrelated to this plan; leave it alone.
