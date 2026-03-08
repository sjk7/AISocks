// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it. PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

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
