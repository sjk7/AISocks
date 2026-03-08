# Plan: Extract HtmlEscape utility

## What & Why

`HttpPollServer` has `escapeHtml()` and `HttpFileServer` has a nearly identical `htmlEscape()`.
Both are pure functions with no dependency on any server state. Having the same logic in two
places in the inheritance hierarchy is a maintenance hazard (one could be updated and not the
other), and neither belongs in a protocol/server class — HTML encoding is a presentation-layer
utility.

The fix: one `inline` free function `HtmlEscape::encode()` in a new header. Both classes
delegate to it, and the duplicate code is removed.

---

## Relevant Files

- `lib/include/HtmlEscape.h` — **new file** (header-only)
- `lib/include/HttpPollServer.h` — remove `escapeHtml()` declaration; add `#include "HtmlEscape.h"`
- `lib/src/HttpPollServer.cpp` — replace `escapeHtml()` definition with a thin forwarder (or
  remove if no longer called directly; check usages first)
- `lib/include/HttpFileServer.h` — remove `htmlEscape()` declaration; add `#include "HtmlEscape.h"`
- `lib/src/HttpFileServer.cpp` — update all call sites from `htmlEscape(x)` to `HtmlEscape::encode(x)`

---

## Step 1 — Create `lib/include/HtmlEscape.h`

```cpp
#ifndef AISOCKS_HTML_ESCAPE_H
#define AISOCKS_HTML_ESCAPE_H

#include <string>

namespace aiSocks {
namespace HtmlEscape {

// ---------------------------------------------------------------------------
// Encode special HTML characters to prevent reflected XSS.
// Replaces & < > " ' with their named/numeric entities.
// Pure function — no allocations beyond the returned string.
// ---------------------------------------------------------------------------
inline std::string encode(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (char c : input) {
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&#39;";  break;
            default:   out += c;        break;
        }
    }
    return out;
}

} // namespace HtmlEscape
} // namespace aiSocks

#endif // AISOCKS_HTML_ESCAPE_H
```

---

## Step 2 — Update `lib/include/HttpPollServer.h`

Add include:
```cpp
#include "HtmlEscape.h"
```

Remove the `protected` declaration:
```cpp
/// (Used for Reflected XSS prevention)
static std::string escapeHtml(const std::string& input);
```

---

## Step 3 — Update `lib/src/HttpPollServer.cpp`

Check all usages first:
```bash
grep -n "escapeHtml" lib/src/HttpPollServer.cpp lib/src/HttpFileServer.cpp
```

Remove (or replace) the definition:
```cpp
std::string HttpPollServer::escapeHtml(const std::string& input) { ... }
```

If any call site in `HttpPollServer.cpp` uses `escapeHtml(x)`, replace with
`HtmlEscape::encode(x)`.

---

## Step 4 — Update `lib/include/HttpFileServer.h`

Add include (if not already transitively present):
```cpp
#include "HtmlEscape.h"
```

Remove the declaration:
```cpp
static std::string htmlEscape(const std::string& str);
```

---

## Step 5 — Update `lib/src/HttpFileServer.cpp`

Remove the definition of `HttpFileServer::htmlEscape()`.

Replace all call sites:
- `htmlEscape(status)` → `HtmlEscape::encode(status)`
- `htmlEscape(message)` → `HtmlEscape::encode(message)`
- `htmlEscape(name)` → `HtmlEscape::encode(name)`

The call sites are in `generateErrorHtml()` and `generateDirectoryListing()`.

---

## Verification

```bash
# 1. Confirm no remaining references to the old names
grep -rn "escapeHtml\|htmlEscape" lib/

# 2. Build
cmake --build build_Mac_arm --config Debug 2>&1 | tail -10

# 3. Tests
cd build_Mac_arm && ctest --output-on-failure -j4
# expect: 100% pass, 30/30
```

---

## Scope / Constraints

- `HtmlEscape.h` is header-only — no CMakeLists change needed.
- The encoding table is identical in both existing implementations; the unified
  version is not a behaviour change.
- `HttpFileServer::htmlEscape` is `static` — no virtual dispatch, safe to remove.
- `HttpPollServer::escapeHtml` is `protected static` — check that no test or
  derived class outside the main source tree calls it before removing.
