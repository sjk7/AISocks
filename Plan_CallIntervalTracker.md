# Plan: Extract CallIntervalTracker from HttpPollServer

## What & Why

`HttpPollServer` contains a self-contained performance metrics system with its own private
mutable state (`intervals_`, `call_count_`, `last_call_`, `last_print_`, `first_output_done_`)
and its own logic (accumulate intervals, compute average, throttle output). This is a separate
concern from HTTP framing and has no dependency on any HTTP state. It bloats every
`HttpPollServer` instance regardless of whether the caller wants metrics, and gives the
class a second reason to change.

The fix: extract a `CallIntervalTracker` value type. `HttpPollServer` holds one as a member
and delegates the `onIdle()` body to it.

---

## Relevant Files

- `lib/include/HttpPollServer.h` — remove 5 private members; add `CallIntervalTracker tracker_`
- `lib/src/HttpPollServer.cpp` — hollow out `onIdle()` body to a single delegating call
- `lib/include/CallIntervalTracker.h` — **new file** (header-only, no .cpp needed)

---

## Step 1 — Create `lib/include/CallIntervalTracker.h`

New header-only struct. Depends only on `<chrono>`, `<vector>`, `<cstdio>`.

```cpp
#ifndef AISOCKS_CALL_INTERVAL_TRACKER_H
#define AISOCKS_CALL_INTERVAL_TRACKER_H

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <vector>

namespace aiSocks {

// ---------------------------------------------------------------------------
// CallIntervalTracker
//
// Records how often a periodic callback fires and prints a throttled summary
// to stdout.  Extracted from HttpPollServer::onIdle() so that the metrics
// concern lives independently of HTTP framing state.
//
// Usage:
//   CallIntervalTracker tracker_;
//   // inside the periodic callback:
//   tracker_.record(clientCount(), peakClientCount());
// ---------------------------------------------------------------------------
struct CallIntervalTracker {
    // Record one call and print a summary when the throttle interval elapses.
    // clientCount / peakCount are passed in by the caller so this struct has
    // no dependency on ServerBase.
    void record(size_t clientCount, size_t peakCount) {
        using Clock = std::chrono::steady_clock;
        using Sec   = std::chrono::duration<double>;
        using Ms    = std::chrono::duration<double, std::milli>;

        const auto now = Clock::now();
        intervals_.push_back(Ms(now - last_call_).count());
        last_call_ = now;
        ++call_count_;

        // Print at most once per 60 s after the first output; before that,
        // print after 0.5 s so the operator gets quick feedback on startup.
        const double print_interval = first_output_done_ ? 60.0 : 0.5;
        if (Sec(now - last_print_).count() >= print_interval) {
            if (!intervals_.empty()) {
                double sum = 0;
                for (double v : intervals_) sum += v;
                printf("onIdle() called %d times, avg interval: %.1fms  "
                       "clients: %zu  peak: %zu\n",
                    call_count_,
                    sum / static_cast<double>(intervals_.size()),
                    clientCount, peakCount);
            }
            intervals_.clear();
            call_count_        = 0;
            last_print_        = now;
            first_output_done_ = true;
        }
    }

private:
    using Clock = std::chrono::steady_clock;

    Clock::time_point   last_call_         = Clock::now();
    Clock::time_point   last_print_        = Clock::now();
    std::vector<double> intervals_;
    int                 call_count_        = 0;
    bool                first_output_done_ = false;
};

} // namespace aiSocks

#endif // AISOCKS_CALL_INTERVAL_TRACKER_H
```

---

## Step 2 — Update `lib/include/HttpPollServer.h`

Add include at the top of the include block:
```cpp
#include "CallIntervalTracker.h"
```

Remove these 5 private members:
```cpp
std::chrono::steady_clock::time_point last_call_
    = std::chrono::steady_clock::now();
std::chrono::steady_clock::time_point last_print_
    = std::chrono::steady_clock::now();
std::vector<double> intervals_;
int call_count_ = 0;
bool first_output_done_ = false;
```

Replace with:
```cpp
CallIntervalTracker tracker_;
```

Also remove the `<vector>` include if it is no longer needed elsewhere in the header
(check first — `HttpClientState` does not use it, so it should be safe to remove).

---

## Step 3 — Update `lib/src/HttpPollServer.cpp`

Replace the entire body of `HttpPollServer::onIdle()`:

**Before:**
```cpp
ServerResult HttpPollServer::onIdle() {
    auto now = std::chrono::steady_clock::now();
    auto interval
        = std::chrono::duration<double, std::milli>(now - last_call_).count();
    last_call_ = now;
    intervals_.push_back(interval);
    ++call_count_;

    auto since_print = std::chrono::duration<double>(now - last_print_).count();
    double print_interval = first_output_done_ ? 60.0 : 0.5;

    if (since_print >= print_interval) {
        if (!intervals_.empty()) {
            double sum = 0;
            for (double v : intervals_) sum += v;
            printf("onIdle() called %d times, avg interval: %.1fms  "
                   "clients: %zu  peak: %zu\n",
                call_count_, sum / static_cast<double>(intervals_.size()),
                static_cast<size_t>(clientCount()),
                static_cast<size_t>(peakClientCount()));
        }
        intervals_.clear();
        call_count_ = 0;
        last_print_ = now;
        first_output_done_ = true;
    }

    return ServerBase<HttpClientState>::onIdle();
}
```

**After:**
```cpp
ServerResult HttpPollServer::onIdle() {
    tracker_.record(clientCount(), peakClientCount());
    return ServerBase<HttpClientState>::onIdle();
}
```

---

## Verification

```bash
# 1. Build all targets
cmake --build build_Mac_arm --config Debug 2>&1 | tail -10
# expect: [N/N] ... zero errors

# 2. Run tests
cd build_Mac_arm && ctest --output-on-failure -j4
# expect: 100% tests passed, 0 tests failed out of 30

# 3. Smoke-check: onIdle output still appears
./build_Mac_arm/aiSocksExample &
sleep 2 && kill %1
# expect: "onIdle() called N times, avg interval: X.Xms ..." on stdout
```

---

## Scope / Constraints

- `CallIntervalTracker` is header-only — no CMakeLists change required.
- `escapeHtml` and build-info methods (`buildOS`, `buildKind`, `printBuildInfo`) are
  **not** touched here — see `Plan_BuildInfo.md`.
- Output format of the `printf` is preserved byte-for-byte.
- `CallIntervalTracker` is a plain `struct` (public by default), consistent with the
  lightweight value-type style used elsewhere (e.g. `HttpClientState`).
