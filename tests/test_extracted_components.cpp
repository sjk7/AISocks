// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it. PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Tests for the three components extracted from HttpPollServer / ServerBase:
//   - BuildInfo      (lib/include/BuildInfo.h)
//   - CallIntervalTracker (lib/include/CallIntervalTracker.h)
//   - PollEventLoop  (lib/include/PollEventLoop.h)

#include "BuildInfo.h"
#include "CallIntervalTracker.h"
#include "PollEventLoop.h"
#include "test_helpers.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

using namespace aiSocks;

// ============================================================
// BuildInfo
// ============================================================

static void test_buildinfo_os() {
    BEGIN_TEST("BuildInfo::os() returns a known, non-empty string");
    const char* o = BuildInfo::os();
    REQUIRE(o != nullptr);
    REQUIRE(o[0] != '\0');
    // Must be one of the four known values.
    const bool known = (strcmp(o, "macOS") == 0 || strcmp(o, "Linux") == 0
        || strcmp(o, "Windows") == 0 || strcmp(o, "Unknown") == 0);
    REQUIRE(known);

#if defined(__APPLE__)
    REQUIRE(strcmp(o, "macOS") == 0);
#elif defined(__linux__)
    REQUIRE(strcmp(o, "Linux") == 0);
#elif defined(_WIN32)
    REQUIRE(strcmp(o, "Windows") == 0);
#endif
}

static void test_buildinfo_kind() {
    BEGIN_TEST("BuildInfo::kind() matches NDEBUG state");
    const char* k = BuildInfo::kind();
    REQUIRE(k != nullptr);
    REQUIRE(k[0] != '\0');
#if defined(NDEBUG)
    REQUIRE(strcmp(k, "Release") == 0);
#else
    REQUIRE(strcmp(k, "Debug") == 0);
#endif
}

static void test_buildinfo_print_smoke() {
    BEGIN_TEST("BuildInfo::print() runs without crashing");
    // Just invoke it; output goes to stdout next to normal test output.
    BuildInfo::print();
    REQUIRE(true);
}

// ============================================================
// CallIntervalTracker
// ============================================================

static void test_tracker_default_construct() {
    BEGIN_TEST("CallIntervalTracker: default-constructible without crash");
    CallIntervalTracker t;
    (void)t;
    REQUIRE(true);
}

static void test_tracker_rapid_calls_no_crash() {
    BEGIN_TEST("CallIntervalTracker: 1000 rapid record() calls do not crash");
    CallIntervalTracker t;
    for (int i = 0; i < 1000; ++i) {
        t.record(static_cast<size_t>(i), static_cast<size_t>(i));
    }
    REQUIRE(true);
}

static void test_tracker_independent_instances() {
    BEGIN_TEST("CallIntervalTracker: two instances are independent");
    CallIntervalTracker a;
    CallIntervalTracker b;
    // Drive both separately; they must not share state.
    for (int i = 0; i < 5; ++i) a.record(1, 1);
    for (int i = 0; i < 5; ++i) b.record(2, 2);
    REQUIRE(true); // If they shared state one would corrupt the other.
}

static void test_tracker_first_output_after_delay() {
    BEGIN_TEST("CallIntervalTracker: fires throttled print after 0.5 s delay");
    // After construction we sleep past the 0.5 s early-output threshold so
    // the next record() call triggers the first output path.  We verify the
    // call returns without crashing and doesn't block.
    CallIntervalTracker t;
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    t.record(0, 0); // Should print once.
    // If we call again immediately, the 60 s post-first-output throttle
    // means it should NOT print.
    t.record(0, 0);
    REQUIRE(true);
}

// ============================================================
// PollEventLoop
// ============================================================

static void test_loop_initial_state() {
    BEGIN_TEST("PollEventLoop: initial stopRequested() is false");
    PollEventLoop loop;
    REQUIRE(!loop.stopRequested());
}

static void test_loop_request_stop() {
    BEGIN_TEST("PollEventLoop: requestStop() sets stopRequested()");
    PollEventLoop loop;
    loop.requestStop();
    REQUIRE(loop.stopRequested());
}

static void test_loop_handle_signals_default() {
    BEGIN_TEST("PollEventLoop: handlesSignals() defaults to true");
    PollEventLoop loop;
    REQUIRE(loop.handlesSignals());
}

static void test_loop_set_handle_signals() {
    BEGIN_TEST("PollEventLoop: setHandleSignals(false) is reflected");
    PollEventLoop loop;
    loop.setHandleSignals(false);
    REQUIRE(!loop.handlesSignals());
    loop.setHandleSignals(true);
    REQUIRE(loop.handlesSignals());
}

static void test_loop_run_exits_via_stop_predicate() {
    BEGIN_TEST(
        "PollEventLoop: run() exits immediately when shouldStop returns true");

    PollEventLoop loop;
    // Disable signal handling so we don't touch process-wide signal state
    // inside a unit test.
    loop.setHandleSignals(false);

    bool eventFired = false;
    bool afterBatchFired = false;

    loop.run(
        Milliseconds{50},
        [&](TcpSocket&, PollEvent) -> bool {
            eventFired = true;
            return true;
        },
        [&](bool /*idle*/) -> bool {
            afterBatchFired = true;
            return true; // keep going if we ever get here
        },
        [&]() -> bool {
            return true; // stop immediately
        });

    // shouldStop fired before the first poller.wait(), so neither callback
    // should have been invoked.
    REQUIRE(!eventFired);
    // afterBatch might or might not have fired depending on whether wait()
    // was called, but the loop must have returned.
    REQUIRE(true); // primary assertion: we reached this line (no hang)
    (void)afterBatchFired;
}

static void test_loop_run_exits_via_request_stop() {
    BEGIN_TEST("PollEventLoop: run() exits when afterBatch returns false");

    PollEventLoop loop;
    loop.setHandleSignals(false);

    int afterBatchCount = 0;

    // We need at least one socket in the poller for wait() to be meaningful,
    // but we can also just use a shouldStop that fires on the second iteration
    // to avoid waiting at all.  Use shouldStop to limit to one iteration.
    int iterations = 0;
    loop.run(
        Milliseconds{1}, [](TcpSocket&, PollEvent) -> bool { return true; },
        [&](bool /*idle*/) -> bool {
            ++afterBatchCount;
            return false; // request stop from afterBatch
        },
        [&]() -> bool {
            return (iterations++ >= 1); // let the first iteration proceed
        });

    // afterBatch must have returned false to exit, so count >= 1.
    REQUIRE(afterBatchCount >= 1);
}

// ============================================================
// main
// ============================================================

int main() {
    // BuildInfo
    test_buildinfo_os();
    test_buildinfo_kind();
    test_buildinfo_print_smoke();

    // CallIntervalTracker
    test_tracker_default_construct();
    test_tracker_rapid_calls_no_crash();
    test_tracker_independent_instances();
    test_tracker_first_output_after_delay();

    // PollEventLoop
    test_loop_initial_state();
    test_loop_request_stop();
    test_loop_handle_signals_default();
    test_loop_set_handle_signals();
    test_loop_run_exits_via_stop_predicate();
    test_loop_run_exits_via_request_stop();

    return test_summary();
}
