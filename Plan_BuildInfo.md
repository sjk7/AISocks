# Plan: Extract BuildInfo from HttpPollServer

## What & Why

`HttpPollServer` contains three static methods — `buildOS()`, `buildKind()`,
`printBuildInfo()` — and a virtual `printStartupBanner()` that depend on them.
These are pure compile-time queries with no dependency on instance state, sockets,
HTTP, or any server concept. They are misplaced in the HTTP server class: changing
the output format or adding a new platform requires editing `HttpPollServer`.

The fix: move them to a `namespace BuildInfo` of `inline` free functions in a new
header. `HttpPollServer` calls into that namespace. The virtual `printStartupBanner()`
stays in `HttpPollServer` since it is a server lifecycle hook — it just delegates to
`BuildInfo::print()` in its default implementation.

---

## Relevant Files

- `lib/include/HttpPollServer.h` — remove `buildOS()`, `buildKind()`, keep `printStartupBanner()` and `printBuildInfo()` as thin forwarders
- `lib/src/HttpPollServer.cpp` — update `printBuildInfo()` and `printStartupBanner()` bodies
- `lib/include/BuildInfo.h` — **new file** (header-only, no .cpp needed)

---

## Step 1 — Create `lib/include/BuildInfo.h`

```cpp
#ifndef AISOCKS_BUILD_INFO_H
#define AISOCKS_BUILD_INFO_H

#include <cstdio>

namespace aiSocks {
namespace BuildInfo {

// ---------------------------------------------------------------------------
// Compile-time platform and build-type queries.
// All functions are inline constexpr / inline — zero runtime overhead.
// ---------------------------------------------------------------------------

inline constexpr const char* os() noexcept {
#if defined(__APPLE__)
    return "macOS";
#elif defined(__linux__)
    return "Linux";
#elif defined(_WIN32)
    return "Windows";
#else
    return "Unknown";
#endif
}

inline constexpr const char* kind() noexcept {
#if defined(NDEBUG)
    return "Release";
#else
    return "Debug";
#endif
}

// Prints a one-line build summary to stdout.
inline void print() {
    printf("Built: %s %s  |  OS: %s  |  Build: %s\n",
        __DATE__, __TIME__, os(), kind());
}

} // namespace BuildInfo
} // namespace aiSocks

#endif // AISOCKS_BUILD_INFO_H
```

---

## Step 2 — Update `lib/include/HttpPollServer.h`

Add include near the top:
```cpp
#include "BuildInfo.h"
```

Remove the two `protected` static methods entirely:
```cpp
static const char* buildOS() noexcept { ... }
static const char* buildKind() noexcept { ... }
```

`printBuildInfo()` stays declared as a `static` public method — it now just calls
`BuildInfo::print()`. Its declaration in the header is unchanged; only the body in
the `.cpp` changes (Step 3).

`printStartupBanner()` stays declared as `virtual void` — unchanged.

---

## Step 3 — Update `lib/src/HttpPollServer.cpp`

Add include at top if not already transitively included:
```cpp
#include "BuildInfo.h"
```

Update the two function bodies:

**`printBuildInfo()`** — before:
```cpp
void HttpPollServer::printBuildInfo() {
    printf("Built: %s %s  |  OS: %s  |  Build: %s\n", __DATE__, __TIME__,
        buildOS(), buildKind());
}
```
After:
```cpp
void HttpPollServer::printBuildInfo() {
    BuildInfo::print();
}
```

**`printStartupBanner()`** — before:
```cpp
void HttpPollServer::printStartupBanner() {
    printBuildInfo();
}
```
After (unchanged in behaviour, but now the intent is explicit):
```cpp
void HttpPollServer::printStartupBanner() {
    BuildInfo::print();
}
```

---

## Verification

```bash
# 1. Build all targets
cmake --build build_Mac_arm --config Debug 2>&1 | tail -10
# expect: zero errors, all targets link

# 2. Run tests
cd build_Mac_arm && ctest --output-on-failure -j4
# expect: 100% tests passed, 0 tests failed out of 30

# 3. Confirm build info still prints (used in several examples)
./build_Mac_arm/aiSocksExample 2>&1 | head -5
# expect: "Built: ... OS: macOS  |  Build: Debug" line appears
```

---

## Scope / Constraints

- `BuildInfo.h` is header-only — no CMakeLists change required.
- `escapeHtml` is **not** touched here — it is a separate HTTP/security concern
  and is candidates for its own `HtmlEscape` utility.
- `printBuildInfo()` remains a public `static` method on `HttpPollServer` for
  backward compatibility with any calling code. It becomes a one-line forwarder.
- `printStartupBanner()` remains `virtual` — derived classes (e.g. `HttpFileServer`)
  may override it. No change to the override mechanism.
- `buildOS()` and `buildKind()` are removed from `HttpPollServer` entirely since no
  code outside `printBuildInfo` / `printStartupBanner` calls them. Verify with:
  ```bash
  grep -r "buildOS\|buildKind" lib/ tests/ examples/
  ```
  If any hits appear outside `HttpPollServer.cpp`, keep them as `protected` forwarders
  to `BuildInfo::os()` / `BuildInfo::kind()` rather than removing them.
