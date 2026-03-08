# Plan: Extract KeepAliveTimeoutManager from ServerBase

## What & Why

`ServerBase` contains ~80 lines of a self-contained lazy-deletion min-heap algorithm
(`TimeoutEntry`, `pushTimeoutEntry()`, `sweepTimeouts()`) plus a separate load-tuning
state machine (`inHighLoadMode_`, `normalKeepAliveTimeout_`, `lastSweepTime_`,
`HIGH_LOAD_THRESHOLD`). These are distinct sub-problems — neither the heap algorithm
nor the load-mode switching has any semantic connection to event-loop orchestration
or client lifecycle management.

The fix: extract a `KeepAliveTimeoutManager` class that owns the heap, the sweep,
and the load tuning. `ServerBase` holds it as a value member and calls two methods:
`touch(fd, activity)` and `sweep(loop, onExpire callback)`.

---

## Relevant Files

- `lib/include/KeepAliveTimeoutManager.h` — **new file**
- `lib/include/ServerBase.h` — remove heap/load-tuning fields and methods; add member
  `KeepAliveTimeoutManager timeouts_`

---

## Step 1 — Create `lib/include/KeepAliveTimeoutManager.h`

The class takes a generic `OnExpire` callback in `sweep()` so it has no dependency on
`ServerBase` or `PollEventLoop`.

```cpp
#ifndef AISOCKS_KEEPALIVE_TIMEOUT_MANAGER_H
#define AISOCKS_KEEPALIVE_TIMEOUT_MANAGER_H

#include "PollEventLoop.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace aiSocks {

// ---------------------------------------------------------------------------
// KeepAliveTimeoutManager
//
// Tracks per-connection idle timeouts using a lazy-deletion min-heap.
// Also manages dynamic timeout adjustment under high client load.
//
// Lazy-deletion design:
//   touch() pushes a new heap entry with a refreshed expiry; the old entry
//   stays in the heap and is discarded by sweep() when its lastActivitySnap
//   no longer matches the live record.  This gives O(log n) touch and O(1)
//   fast-path sweep (single comparison when nothing has expired).
//
//   To bound heap growth under high throughput, touch() only pushes a new
//   entry once per timeout_/4 per connection.  Worst-case accuracy: +25%.
//
// Load tuning:
//   When clientCount exceeds HIGH_LOAD_THRESHOLD, the active timeout is
//   reduced to an aggressive 5 s value.  It is restored when load drops.
// ---------------------------------------------------------------------------
class KeepAliveTimeoutManager {
public:
    using Clock = std::chrono::steady_clock;

    // reserve() pre-sizes internal vectors; pass expected max client count.
    void reserve(size_t capacity) {
        heap_.reserve(capacity * 2);
    }

    // Record activity for `fd`.  lastActivity is the timestamp to use (pass
    // steady_clock::now() from the caller to avoid redundant clock reads).
    void touch(uintptr_t fd, Clock::time_point lastActivity,
               Clock::time_point& lastTimeoutPush) {
        if (timeout_.count() == 0) return;
        const auto sincePush = lastActivity - lastTimeoutPush;
        const int64_t DOWNSAMPLE = 4;
        if (sincePush >= timeout_ / DOWNSAMPLE) {
            pushEntry(fd, lastActivity);
            lastTimeoutPush = lastActivity;
        }
    }

    // Push the initial entry for a newly accepted connection.
    void onAccept(uintptr_t fd, Clock::time_point activity) {
        if (timeout_.count() > 0) pushEntry(fd, activity);
    }

    // Sweep expired entries.  For each genuinely expired fd, calls
    //   onExpire(fd) — the caller is responsible for closing the socket.
    // Returns the number of connections closed.
    template <typename OnExpire>
    size_t sweep(const std::vector<Clock::time_point>& lastActivityByFd,
                 const std::vector<bool>& fdActive,
                 OnExpire&& onExpire) {
        if (timeout_.count() == 0 || heap_.empty()) return 0;
        const auto now = Clock::now();
        if (heap_.front().expiry > now) return 0;

        size_t closed = 0;
        while (!heap_.empty() && heap_.front().expiry <= now) {
            Entry entry = heap_.front();
            std::pop_heap(heap_.begin(), heap_.end());
            heap_.pop_back();

            // Stale check 1: fd no longer active.
            if (entry.fd >= fdActive.size() || !fdActive[entry.fd]) continue;
            // Stale check 2: activity was refreshed after this entry was pushed.
            if (entry.fd >= lastActivityByFd.size() ||
                lastActivityByFd[entry.fd] != entry.lastActivitySnap) continue;

            onExpire(entry.fd);
            ++closed;
        }
        return closed;
    }

    // Adjust timeout based on current client count.  Call once per event batch.
    void adjustForLoad(size_t clientCount) {
        constexpr size_t HIGH_LOAD_THRESHOLD = 256;
        constexpr auto   AGGRESSIVE = std::chrono::milliseconds{5000};

        if (!inHighLoad_ && clientCount > HIGH_LOAD_THRESHOLD) {
            normal_  = timeout_;
            timeout_ = AGGRESSIVE;
            inHighLoad_ = true;
            printf("[KeepAliveTimeoutManager] High load (%zu clients): "
                   "timeout -> %lld ms\n",
                clientCount, static_cast<long long>(AGGRESSIVE.count()));
        } else if (inHighLoad_ && clientCount <= HIGH_LOAD_THRESHOLD) {
            timeout_    = normal_;
            inHighLoad_ = false;
            printf("[KeepAliveTimeoutManager] Load normalised (%zu clients): "
                   "timeout -> %lld ms\n",
                clientCount, static_cast<long long>(normal_.count()));
        }
    }

    void setTimeout(std::chrono::milliseconds t) {
        if (inHighLoad_) normal_ = t;
        else { timeout_ = t; normal_ = t; }
    }
    std::chrono::milliseconds getTimeout() const { return timeout_; }

    // Throttle helper: returns true if enough time has passed to warrant a sweep.
    bool sweepDue(size_t clientCount) {
        const auto now = Clock::now();
        if (clientCount < 1000 ||
            (now - lastSweep_) >= std::chrono::milliseconds{100}) {
            lastSweep_ = now;
            return true;
        }
        return false;
    }

private:
    struct Entry {
        Clock::time_point expiry;
        Clock::time_point lastActivitySnap;
        uintptr_t         fd;

        // Inverted for min-heap behaviour via std::push/pop_heap (max-heap).
        bool operator<(const Entry& o) const noexcept {
            return expiry > o.expiry;
        }
    };

    void pushEntry(uintptr_t fd, Clock::time_point activity) {
        heap_.push_back({activity + timeout_, activity, fd});
        std::push_heap(heap_.begin(), heap_.end());
    }

    std::vector<Entry>         heap_;
    std::chrono::milliseconds  timeout_{65'000};
    std::chrono::milliseconds  normal_{65'000};
    Clock::time_point          lastSweep_{};
    bool                       inHighLoad_{false};
};

} // namespace aiSocks

#endif // AISOCKS_KEEPALIVE_TIMEOUT_MANAGER_H
```

> **Note on the sweep() interface:** The current `ServerBase` sweeps by calling
> `eraseClient(fd)` and `loop_.remove()` directly. The cleanest seam is an `onExpire`
> lambda passed at the call site in `ServerBase`'s `afterBatch` closure — identical
> to the pattern already used for `PollEventLoop`. See Step 2.

---

## Step 2 — Update `lib/include/ServerBase.h`

### Add include
```cpp
#include "KeepAliveTimeoutManager.h"
```

### Remove from `private:` section

Fields to remove:
```cpp
SteadyClock::time_point lastSweepTime_{};
std::vector<TimeoutEntry> timeout_heap_;
std::chrono::milliseconds keepAliveTimeout_{65'000};
std::chrono::milliseconds normalKeepAliveTimeout_{65'000};
bool inHighLoadMode_{false};
```

Methods to remove:
```cpp
struct TimeoutEntry { ... };          // ~15 lines
void pushTimeoutEntry(...) { ... }    // ~8 lines
void sweepTimeouts(PollEventLoop&) { ... }  // ~40 lines
```

### Add member
```cpp
KeepAliveTimeoutManager timeouts_;
```

### Update `touchClient()`

Before:
```cpp
void touchClient(const TcpSocket& sock) {
    ...
    ce->lastActivity = now;
    if (keepAliveTimeout_.count() > 0) {
        const auto sincePush = now - ce->lastTimeoutPush;
        if (sincePush >= keepAliveTimeout_ / PVS_FIX_DOWNSAMPLE_FACTOR) {
            pushTimeoutEntry(fd, now);
            ce->lastTimeoutPush = now;
        }
    }
}
```

After:
```cpp
void touchClient(const TcpSocket& sock) {
    const uintptr_t fd = sock.getNativeHandle();
    ClientEntry* ce = findClient(fd);
    if (!ce) return;
    const auto now = SteadyClock::now();
    ce->lastActivity = now;
    timeouts_.touch(fd, now, ce->lastTimeoutPush);
}
```

### Update `setKeepAliveTimeout()` / `getKeepAliveTimeout()`

```cpp
void setKeepAliveTimeout(Milliseconds t) {
    timeouts_.setTimeout(std::chrono::milliseconds{t.count});
}
Milliseconds getKeepAliveTimeout() const {
    return Milliseconds{timeouts_.getTimeout().count()};
}
```

### Update `emplaceClient()` — initial timeout push

Replace the direct `pushTimeoutEntry(key, ce.lastActivity)` call with:
```cpp
timeouts_.onAccept(key, ce.lastActivity);
```

### Update `afterBatch` lambda inside `run()`

Replace the sweep + load-tuning block:
```cpp
timeouts_.adjustForLoad(clientFds_.size());

if (timeouts_.sweepDue(clientFds_.size())) {
    // NOTE: KeepAliveTimeoutManager::sweep() needs lastActivity and active
    // state vectors.  The cleanest approach for now is to keep a thin
    // forwarding lambda that calls eraseClient/loop_.remove directly:
    size_t closed = 0;
    // ... sweep with onExpire lambda (see note below)
    if (closed > 0) onClientsTimedOut(closed);

    if (!accepting && maxClients != ClientLimit::Unlimited
        && clientFds_.size() < static_cast<size_t>(maxClients)) {
        if (loop_.add(*listener_, PollEvent::Readable | PollEvent::Error))
            accepting = true;
    }
}
```

> **Implementation note:** `KeepAliveTimeoutManager::sweep()` as designed above
> requires `lastActivityByFd` and `fdActive` vectors indexed by fd — which are already
> tracked in `ClientEntry`. The simplest migration is to pass a lambda that looks up
> the `ClientEntry` directly, matching how `sweepTimeouts()` worked before:
> ```cpp
> size_t closed = timeouts_.sweepRaw(
>     [&](uintptr_t fd) -> std::pair<bool, SteadyClock::time_point> {
>         ClientEntry* ce = findClient(fd);
>         if (!ce) return {false, {}};
>         return {true, ce->lastActivity};
>     },
>     [&](uintptr_t fd) {
>         ClientEntry* ce = findClient(fd);
>         onDisconnect(ce->data);
>         ce->socket->shutdown(ShutdownHow::Both);
>         loop_.remove(*ce->socket);
>         eraseClient(fd);
>         ++closed;
>     }
> );
> ```
> Adjust `sweep()` signature accordingly during implementation.

---

## Verification

```bash
# 1. Build
cmake --build build_Mac_arm --config Debug 2>&1 | tail -10

# 2. Tests — especially timeout-related ones
cd build_Mac_arm && ctest --output-on-failure -j4
# expect: 100% pass, 30/30

# 3. Confirm high-load log still appears under load
# (optional: run the concurrent_test.py script)
```

---

## Scope / Constraints

- `KeepAliveTimeoutManager.h` is header-only — no CMakeLists change needed.
- `ClientEntry` retains `lastActivity` and `lastTimeoutPush` fields (they are
  needed by `touchClient`); only the heap/sweep/load logic moves.
- The `onClientsTimedOut()` virtual hook stays in `ServerBase` — it is a server
  lifecycle callback, not a timeout-management concern.
- `lastSweepTime_` moves into `KeepAliveTimeoutManager` as the `lastSweep_` field.
