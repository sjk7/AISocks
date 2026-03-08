# Plan: Extract HtmlPageGenerator from HttpFileServer

## What & Why

`HttpFileServer` contains `generateErrorHtml()` and `generateDirectoryListing()` —
two methods that produce complete HTML documents from data. These have no dependency
on sockets, HTTP framing, configuration (beyond `hideServerVersion`), or file I/O.
HTML generation is a presentation concern; embedding it in the file server means
changing the error page layout requires editing `HttpFileServer`.

The fix: extract a `HtmlPageGenerator` class (or `namespace`) that accepts only the
data it needs and returns an HTML string. `HttpFileServer` constructs it with the
`hideServerVersion` flag and delegates both methods.

---

## Relevant Files

- `lib/include/HtmlPageGenerator.h` — **new file** (header-only)
- `lib/include/HttpFileServer.h` — remove two method declarations; add include
- `lib/src/HttpFileServer.cpp` — remove two method definitions; update call sites

---

## Step 1 — Create `lib/include/HtmlPageGenerator.h`

```cpp
#ifndef AISOCKS_HTML_PAGE_GENERATOR_H
#define AISOCKS_HTML_PAGE_GENERATOR_H

#include "HtmlEscape.h"
#include "PathHelper.h"
#include "UrlCodec.h"
#include <cstdio>
#include <string>
#include <vector>

namespace aiSocks {

// ---------------------------------------------------------------------------
// HtmlPageGenerator
//
// Generates complete HTML documents for error responses and directory listings.
// Pure presentation logic — no server state, no sockets, no file I/O.
//
// hideServerVersion controls whether the "aiSocks HttpFileServer" footer
// appears at the bottom of generated pages.
// ---------------------------------------------------------------------------
class HtmlPageGenerator {
public:
    explicit HtmlPageGenerator(bool hideServerVersion = true)
        : hideServerVersion_(hideServerVersion) {}

    // Generates a simple error page.
    // `message` is HTML-escaped internally — safe to pass user-derived strings.
    std::string errorPage(
        int code, const std::string& status, const std::string& message) const
    {
        std::string html;
        html.reserve(512);
        html += "<!DOCTYPE html>\n<html><head><title>";
        char cbuf[8];
        std::snprintf(cbuf, sizeof(cbuf), "%d", code);
        html += cbuf;
        html += " ";
        html += HtmlEscape::encode(status);
        html += "</title></head>\n<body><h1>";
        html += cbuf;
        html += " ";
        html += HtmlEscape::encode(status);
        html += "</h1>\n<p>";
        html += HtmlEscape::encode(message);
        html += "</p>\n";
        if (!hideServerVersion_)
            html += "<hr><address>aiSocks HttpFileServer</address>\n";
        html += "</body></html>";
        return html;
    }

    // Generates a directory listing page.
    std::string directoryListing(const std::string& dirPath) const {
        std::vector<PathHelper::DirEntry> entries
            = PathHelper::listDirectory(dirPath);

        std::string html;
        html.reserve(2048);
        html += "<!DOCTYPE html>\n"
                "<html><head><title>Directory listing</title></head>\n"
                "<body><h1>Directory listing</h1>\n<ul>\n";

        if (entries.empty()) {
            html += "<li>Error reading directory</li>\n";
        } else {
            for (const auto& entry : entries) {
                const std::string& name = entry.name;
                if (name.empty() || name[0] == '.') continue;
                bool isDir = entry.isDirectory;
                html += "<li><a href=\"";
                html += urlEncode(name);
                if (isDir) html += "/";
                html += "\">";
                html += HtmlEscape::encode(name);
                if (isDir) html += "/";
                html += "</a></li>\n";
            }
        }

        html += "</ul>\n";
        if (!hideServerVersion_)
            html += "<hr><address>aiSocks HttpFileServer</address>\n";
        html += "</body></html>";
        return html;
    }

private:
    bool hideServerVersion_;
};

} // namespace aiSocks

#endif // AISOCKS_HTML_PAGE_GENERATOR_H
```

> **Note:** Uses `std::string +=` instead of `StringBuilder` to avoid adding a
> dependency. If `StringBuilder` is available and preferred, swap the body
> accordingly — the output is identical.

---

## Step 2 — Update `lib/include/HttpFileServer.h`

Add include:
```cpp
#include "HtmlPageGenerator.h"
```

Remove declarations:
```cpp
std::string generateErrorHtml(int code, const std::string& status,
    const std::string& message) const;
std::string generateDirectoryListing(const std::string& dirPath) const;
```

Add a private member (constructed from config in the constructor):
```cpp
HtmlPageGenerator htmlGen_;
```

---

## Step 3 — Update `lib/src/HttpFileServer.cpp`

### Constructor — initialise `htmlGen_`

Add to the constructor initialiser list:
```cpp
, htmlGen_(config_.hideServerVersion)
```

Or assign after the config block:
```cpp
htmlGen_ = HtmlPageGenerator(config_.hideServerVersion);
```

### Remove the two method definitions

Remove `HttpFileServer::generateErrorHtml(...)` (~20 lines) and
`HttpFileServer::generateDirectoryListing(...)` (~30 lines).

### Update call sites

```cpp
// Before:
std::string htmlBody = generateErrorHtml(code, status, message);
// After:
std::string htmlBody = htmlGen_.errorPage(code, status, message);

// Before:
std::string htmlBody = generateDirectoryListing(dirPath);
// After:
std::string htmlBody = htmlGen_.directoryListing(dirPath);
```

Call sites are in `sendError()` and `sendDirectoryListing()`.

---

## Verification

```bash
# 1. Build
cmake --build build_Mac_arm --config Debug 2>&1 | tail -10

# 2. Tests
cd build_Mac_arm && ctest --output-on-failure -j4
# expect: 100% pass, 30/30

# 3. Smoke — request a missing file and verify the HTML error page renders
curl http://localhost:<port>/nonexistent.html
# expect: well-formed HTML with "404 Not Found"

# 4. Smoke — request a directory and verify listing renders
curl http://localhost:<port>/
```

---

## Scope / Constraints

- `HtmlPageGenerator.h` is header-only — no CMakeLists change needed.
- Depends on `HtmlEscape.h` (see Plan_HtmlEscape.md) and the existing `PathHelper`
  and `UrlCodec` headers which are already included transitively.
- `hideServerVersion` is the only config value that affects HTML output; the
  generator does not need the whole `Config` struct.
- If `Plan_HtmlEscape.md` has not yet been applied, replace `HtmlEscape::encode(x)`
  calls with the inline escaping loop as a temporary measure.
