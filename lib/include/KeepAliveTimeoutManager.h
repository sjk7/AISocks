// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#pragma once

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
//   stays in the heap and is discarded by sweepRaw() when its
//   lastActivitySnap no longer matches the live record.  This gives
//   O(log n) touch and O(1) fast-path sweep (single comparison when nothing
//   has expired).
//
//   To bound heap growth under high throughput, touch() only pushes a new
//   entry once per timeout/4 per connection.  Worst-case accuracy: +25%.
//
// Load tuning:
//   When clientCount exceeds HIGH_LOAD_THRESHOLD, the active timeout is
//   reduced to an aggressive 5 s value.  It is restored when load drops.
// ---------------------------------------------------------------------------
class KeepAliveTimeoutManager {
    public:
    using Clock = std::chrono::steady_clock;

    // Pre-size internal vectors; pass expected max client count.
    void reserve(size_t capacity) { heap_.reserve(capacity * 2); }

    // Record activity for `fd`.  lastActivity is the timestamp to use (pass
    // steady_clock::now() from the caller to avoid redundant clock reads).
    // lastTimeoutPush is the per-client throttle timestamp (stored in
    // ClientEntry); it is updated in-place when a heap entry is pushed.
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

    // Sweep expired entries.
    //
    // query(fd)    — returns std::pair<bool, Clock::time_point>:
    //                  .first  = true if the client is still active
    //                  .second = client's current lastActivity timestamp
    // onExpire(fd) — called for each genuinely expired fd; responsible for
    //                closing the socket and removing the client entry.
    //
    // Returns the number of connections that were expired.
    template <typename Query, typename OnExpire>
    size_t sweepRaw(Query&& query, OnExpire&& onExpire) {
        if (timeout_.count() == 0 || heap_.empty()) return 0;
        const auto now = Clock::now();
        if (heap_.front().expiry > now) return 0;

        size_t closed = 0;
        while (!heap_.empty() && heap_.front().expiry <= now) {
            Entry entry = heap_.front();
            std::pop_heap(heap_.begin(), heap_.end());
            heap_.pop_back();

            auto result = query(entry.fd);
            // Stale check 1: client no longer active.
            if (!result.first) continue;
            // Stale check 2: activity was refreshed after this entry was
            // pushed.
            if (result.second != entry.lastActivitySnap) continue;

            onExpire(entry.fd);
            ++closed;
        }
        return closed;
    }

    // Adjust timeout based on current client count.  Call once per event batch.
    void adjustForLoad(size_t clientCount) {
        constexpr size_t HIGH_LOAD_THRESHOLD = 256;

        if (!inHighLoad_ && clientCount > HIGH_LOAD_THRESHOLD) {
            constexpr auto AGGRESSIVE = std::chrono::milliseconds{5000};
            normal_ = timeout_;
            timeout_ = AGGRESSIVE;
            inHighLoad_ = true;
            printf("[KeepAliveTimeoutManager] High load (%zu clients): "
                   "timeout -> %lld ms\n",
                clientCount, static_cast<long long>(AGGRESSIVE.count()));
        } else if (inHighLoad_ && clientCount <= HIGH_LOAD_THRESHOLD) {
            timeout_ = normal_;
            inHighLoad_ = false;
            printf("[KeepAliveTimeoutManager] Load normalised (%zu clients): "
                   "timeout -> %lld ms\n",
                clientCount, static_cast<long long>(normal_.count()));
        }
    }

    void setTimeout(std::chrono::milliseconds t) {
        if (inHighLoad_)
            normal_ = t;
        else {
            timeout_ = t;
            normal_ = t;
        }
    }

    std::chrono::milliseconds getTimeout() const { return timeout_; }

    // Returns true (and records the sweep time) if enough time has elapsed
    // to warrant running a sweep.  Throttles to at most once per 100 ms
    // when client count >= 1000.
    bool sweepDue(size_t clientCount) {
        if (timeout_.count() == 0 || heap_.empty()) return false;
        const auto now = Clock::now();
        if (clientCount < 1000
            || (now - lastSweep_) >= std::chrono::milliseconds{100}) {
            lastSweep_ = now;
            return true;
        }
        return false;
    }

    private:
    struct Entry {
        Clock::time_point expiry;
        Clock::time_point lastActivitySnap;
        uintptr_t fd;

        // Inverted for min-heap behaviour (std::push/pop_heap build a
        // max-heap).
        bool operator<(const Entry& o) const noexcept {
            return expiry > o.expiry;
        }
    };

    void pushEntry(uintptr_t fd, Clock::time_point activity) {
        heap_.push_back({activity + timeout_, activity, fd});
        std::push_heap(heap_.begin(), heap_.end());
    }

    std::vector<Entry> heap_;
    std::chrono::milliseconds timeout_{65'000};
    std::chrono::milliseconds normal_{65'000};
    Clock::time_point lastSweep_{};
    bool inHighLoad_{false};
};

} // namespace aiSocks
