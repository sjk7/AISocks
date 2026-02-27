// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// ---------------------------------------------------------------------------
// Provability tests for ServerBase's lazy-deletion min-heap keep-alive sweep.
//
// The heap replaced an O(n) linear scan.  These five tests verify the
// critical correctness properties of the new implementation:
//
//  Test 1 — timeout_fires_when_idle
//    An idle connection (no data sent after connect) must be closed by
//    sweepTimeouts() once keepAliveTimeout elapses.
//
//  Test 2 — touch_resets_expiry
//    Sending data causes onReadable → touchClient(), which pushes a *new*
//    TimeoutEntry with a refreshed expiry and marks the old one stale via
//    the lastActivitySnap mismatch.  The connection must NOT be closed at
//    the original expiry; it must be closed only after the refreshed window.
//
//  Test 3 — multiple_touches_no_spurious_close
//    Touching a client N times in rapid succession pushes N stale entries
//    plus one live entry onto the heap.  Once the connection finally idles
//    out, exactly ONE close must occur (stale-entry check 1 in sweepTimeouts:
//    after erasing from clients_, the N stale pops find the fd gone and
//    are silently discarded).
//
//  Test 4 — zero_timeout_disables_sweep
//    When keepAliveTimeout == 0 the sweep mechanism is disabled entirely.
//    A client that transmits nothing must still be alive after an interval
//    that would otherwise exceed any sensible timeout.
//
//  Test 5 — only_idle_client_closed
//    With two connected clients, only the one that sends no traffic times
//    out.  The active client (touched repeatedly) must survive until the
//    server is stopped explicitly.
// ---------------------------------------------------------------------------

#include "ServerBase.h"
#include "SocketFactory.h"
#include "TcpSocket.h"
#include "test_helpers.h"
#include <atomic>
#include <chrono>
#include <string>
#include <thread>

using namespace aiSocks;
using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Timing constants
//
// KEEP_ALIVE   — the timeout given to every test server instance, in
//               milliseconds. setKeepAliveTimeout() now takes milliseconds
//               so any positive value works without truncation.
// POLL_TICK    — how often run()'s inner poller loop wakes up.
//               The sweep runs every tick, so POLL_TICK is also the maximum
//               lag between a timeout firing and the connection being closed.
// GRACE        — extra margin added to expected deadlines in test assertions.
//               Covers OS scheduler jitter and CI slowness.
// ---------------------------------------------------------------------------
static constexpr std::chrono::milliseconds KEEP_ALIVE{1000}; // 1 s
static constexpr Milliseconds POLL_TICK{20};
static constexpr auto GRACE = 600ms; // generous for slow machines

// ---------------------------------------------------------------------------
// TimedServer — minimal ServerBase<> subclass used by all tests.
//
// Behaviour:
//   onReadable — drains the socket, calls touchClient() on every non-zero
//                read so that data arrival resets the keep-alive timer.
//   onWritable — not used by these tests; returns KeepConnection.
//   onDisconnect      — increments disconnectCount (all paths).
//   onClientsTimedOut — increments timeoutClosedCount (heap sweep path only).
//
// Both counters are std::atomic so they can be safely read from the test
// thread while the server runs in its own background thread.
// ---------------------------------------------------------------------------
class TimedServer : public ServerBase<int> {
    public:
    std::atomic<int> disconnectCount{0};
    std::atomic<int> timeoutClosedCount{0};

    explicit TimedServer(
        uint16_t port, std::chrono::milliseconds keepAlive = KEEP_ALIVE)
        : ServerBase<int>(ServerBind{"127.0.0.1", Port{port}, 16}) {
        setKeepAliveTimeout(keepAlive);
    }

    // Query the actual OS port (useful when the server bound to port 0).
    uint16_t actualPort() const {
        auto ep = getSocket().getLocalEndpoint();
        return ep.isSuccess() ? ep.value().port.value() : 0;
    }

    protected:
    // Drain available data and touch the client so receipt resets the timer.
    ServerResult onReadable(TcpSocket& sock, int& /*state*/) override {
        char buf[4096];
        for (;;) {
            int n = sock.receive(buf, sizeof(buf));
            if (n > 0) {
                // Receipt of data counts as activity — reset the idle timer.
                touchClient(sock);
            } else if (n == 0) {
                // Clean EOF: peer closed its write end.
                return ServerResult::Disconnect;
            } else {
                // n < 0
                if (sock.getLastError() == SocketError::WouldBlock) break;
                return ServerResult::Disconnect;
            }
        }
        return ServerResult::KeepConnection;
    }

    ServerResult onWritable(TcpSocket& /*sock*/, int& /*state*/) override {
        return ServerResult::KeepConnection;
    }

    void onDisconnect(int& /*state*/) override { ++disconnectCount; }

    // onClientsTimedOut is called by sweepTimeouts() with the count of clients
    // it actually closed this tick — NOT called on normal disconnects.
    void onClientsTimedOut(size_t count) override {
        timeoutClosedCount += static_cast<int>(count);
    }
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Connect a blocking TCP client to 127.0.0.1:<port>.
// Returns a valid TcpSocket on success, invalid on failure.
static TcpSocket connectClient(uint16_t port) {
    return TcpSocket(AddressFamily::IPv4,
        ConnectArgs{"127.0.0.1", Port{port}, Milliseconds{2000}});
}

// Sleep for the given duration (portable shorthand).
static void sleepFor(std::chrono::milliseconds ms) {
    std::this_thread::sleep_for(ms);
}

// Spin-wait until predicate() returns true or the deadline passes.
// Returns true if the condition was met.
template <typename Pred>
static bool waitUntil(Pred predicate, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(10ms);
    }
    return predicate(); // one last check
}

// ---------------------------------------------------------------------------
// Test 1: timeout_fires_when_idle
//
// Property: a client that sends no data after connecting must be closed once
// keepAliveTimeout elapses.  This proves the heap's fast-path correctly
// advances to the slow-path when the front entry actually expires.
// ---------------------------------------------------------------------------
static void test_timeout_fires_when_idle() {
    BEGIN_TEST("timeout heap: idle connection closed after keepAliveTimeout");

    // Bind to port 0: OS assigns a free port atomically. We query it via
    // actualPort() after the constructor has already bound+listened, so no
    // other process can steal the port between allocation and use.
    TimedServer server(0);
    REQUIRE(server.isValid());
    const uint16_t port = server.actualPort();
    REQUIRE(port != 0);

    std::atomic<bool> serverReady{false};
    std::thread serverThread([&] {
        serverReady = true;
        server.run(ClientLimit::Unlimited, POLL_TICK);
    });

    // Wait until the server thread has entered run().
    REQUIRE(waitUntil([&] { return serverReady.load(); }, 1s));
    sleepFor(50ms); // let the poller add the listener socket

    // Connect but never send — the client is purely idle.
    TcpSocket client = connectClient(port);
    REQUIRE(client.isValid());

    // --- assertion A: not yet timed out (well within the window) ---
    sleepFor(500ms); // half of the 1s keep-alive window
    REQUIRE_MSG(server.timeoutClosedCount == 0,
        "no timeout before half the window has elapsed");

    // --- assertion B: timed out (past the window plus grace) ---
    // Total from connect: 500ms + 1000ms + 600ms grace = 2100ms > 1000ms
    // window.
    sleepFor(KEEP_ALIVE + GRACE);
    REQUIRE_MSG(server.timeoutClosedCount >= 1,
        "server closed idle connection after keepAliveTimeout");

    // Clean up: stop the server and join.
    server.requestStop();
    serverThread.join();

    // disconnectCount includes the timeout close, so it must be >= 1 too.
    REQUIRE(server.disconnectCount >= 1);
}

// ---------------------------------------------------------------------------
// Test 2: touch_resets_expiry
//
// Property: sending data triggers touchClient(), which pushes a new heap
// entry with a refreshed expiry and marks the old one stale via the
// lastActivitySnap mismatch check.  The connection must NOT be closed at
// the original expiry time; it must survive until the new window elapses.
//
// Timeline (KEEP_ALIVE = 500ms):
//   t=0       connect  — initial entry pushed: expiry = t+500 = t500
//   t=TOUCH   send 1B  — touchClient pushes new entry: expiry = t+(TOUCH+500)
//                         old stale entry at t500 remains in heap
//   t=500ms   sweep    — pops stale entry (lastActivitySnap mismatch), discards
//   t=TOUCH+500+GRACE  — new entry finally fires, connection closed
// ---------------------------------------------------------------------------
static void test_touch_resets_expiry() {
    BEGIN_TEST(
        "timeout heap: touch mid-window resets expiry, no spurious close");

    // Bind to port 0; query actual port after the server is already listening.
    TimedServer server(0);
    REQUIRE(server.isValid());
    const uint16_t port = server.actualPort();
    REQUIRE(port != 0);

    std::atomic<bool> serverReady{false};
    std::thread serverThread([&] {
        serverReady = true;
        server.run(ClientLimit::Unlimited, POLL_TICK);
    });

    REQUIRE(waitUntil([&] { return serverReady.load(); }, 1s));
    sleepFor(50ms); // let the poller add the listener socket

    TcpSocket client = connectClient(port);
    REQUIRE(client.isValid());

    // At t ≈ 500ms from connect, send data to trigger
    // touchClient(), refreshing the expiry to (now + KEEP_ALIVE).
    static constexpr auto touchAt = 500ms;
    sleepFor(touchAt);
    const char ping[] = "ping";
    int sent = client.send(ping, sizeof(ping) - 1);
    REQUIRE_MSG(sent > 0, "send to trigger touchClient succeeded");
    // Let the server fully process the read (at most one poll tick away).
    sleepFor(std::chrono::milliseconds{POLL_TICK.count} * 3);

    // --- assertion A: not closed at the original expiry ---
    // Total time from connect ≈ 500ms + 60ms + 600ms ≈ 1160ms.  The original
    // expiry was at 1000ms; we have just past it.  The stale entry for that
    // expiry should have been discarded by stale-check 2 in sweepTimeouts.
    sleepFor(KEEP_ALIVE - touchAt); // advance to just past the original expiry
    REQUIRE_MSG(server.timeoutClosedCount == 0,
        "connection NOT closed at original expiry — stale entry discarded");

    // --- assertion B: closed at the refreshed expiry ---
    // Refreshed expiry ≈ 1500ms from connect.  We are at ≈ 1160ms, so sleep
    // the remaining window plus grace.
    sleepFor(touchAt + GRACE);
    REQUIRE_MSG(server.timeoutClosedCount >= 1,
        "connection closed at refreshed expiry after touch");

    server.requestStop();
    serverThread.join();
}

// ---------------------------------------------------------------------------
// Test 3: multiple_touches_no_spurious_close
//
// Property: touching a client N times pushes N stale entries onto the heap
// plus one live entry.  When the live entry fires, exactly one close must
// occur.  The N stale entries for the same fd find the entry gone in
// clients_ (stale-entry check 1) and are silently discarded.
//
// This is the most important invariant for the lazy-deletion approach: the
// heap can accumulate entries but must never close a connection more than
// once, and must never fail to close it.
// ---------------------------------------------------------------------------
static void test_multiple_touches_no_spurious_close() {
    BEGIN_TEST("timeout heap: N rapid touches → exactly one close, no spurious "
               "closes");

    TimedServer server(0);
    REQUIRE(server.isValid());
    // Query port after the bind — the OS has already reserved it; no TOCTOU.
    const uint16_t port = server.actualPort();
    REQUIRE(port != 0);

    std::atomic<bool> serverReady{false};
    std::thread serverThread([&] {
        serverReady = true;
        server.run(ClientLimit::Unlimited, POLL_TICK);
    });

    REQUIRE(waitUntil([&] { return serverReady.load(); }, 2s));
    sleepFor(50ms);

    TcpSocket client = connectClient(port);
    REQUIRE(client.isValid());

    // Send data 5 times in rapid succession.  Each recv on the server side
    // calls touchClient(), pushing 5 new TimeoutEntry values onto the heap
    // (plus the original 1 from accept = 6 total entries for one fd).
    // All but the last are stale; only the last one should ever fire.
    static constexpr int N_TOUCHES = 5;
    for (int i = 0; i < N_TOUCHES; ++i) {
        const char msg[] = "x";
        int s = client.send(msg, 1);
        REQUIRE_MSG(s == 1, "send for touch succeeded");
        // Small gap so each send is a distinct read event on the server.
        sleepFor(30ms);
    }
    // Allow the server to process the last touch.
    sleepFor(std::chrono::milliseconds{POLL_TICK.count} * 3);

    // The last touch happened ≈ 3*POLL_TICK ago.  At this point none of the
    // entries should have expired (the youngest expiry is ~ now + KEEP_ALIVE).
    REQUIRE_MSG(server.timeoutClosedCount == 0,
        "no close immediately after last touch");

    // Now let the refreshed window expire.
    sleepFor(KEEP_ALIVE + GRACE);
    REQUIRE_MSG(server.timeoutClosedCount == 1,
        "exactly one close after idle — no spurious closes from stale entries");
    // onDisconnect should also have been called exactly once.
    REQUIRE_MSG(
        server.disconnectCount == 1, "onDisconnect called exactly once");

    server.requestStop();
    serverThread.join();
}

// ---------------------------------------------------------------------------
// Test 4: zero_timeout_disables_sweep
//
// Property: when keepAliveTimeout is set to 0 the sweep is skipped
// entirely (sweepTimeouts() returns immediately).  Idle clients must remain
// connected indefinitely.
// ---------------------------------------------------------------------------
static void test_zero_timeout_disables_sweep() {
    BEGIN_TEST("timeout heap: keepAliveTimeout=0 disables idle close entirely");

    TimedServer server(0, std::chrono::seconds{0}); // port 0 = OS assigns
    REQUIRE(server.isValid());
    const uint16_t port = server.actualPort();
    REQUIRE(port != 0);

    std::atomic<bool> serverReady{false};
    std::thread serverThread([&] {
        serverReady = true;
        server.run(ClientLimit::Unlimited, POLL_TICK);
    });

    REQUIRE(waitUntil([&] { return serverReady.load(); }, 1s));
    sleepFor(50ms);

    TcpSocket client = connectClient(port);
    REQUIRE(client.isValid());

    // Wait 2× the KEEP_ALIVE window — more than enough for the sweep to
    // have fired if it were enabled.  With keepAlive=0 nothing should happen.
    sleepFor(KEEP_ALIVE * 2 + GRACE);
    REQUIRE_MSG(server.timeoutClosedCount == 0,
        "no timeout close when keepAliveTimeout == 0");
    REQUIRE_MSG(server.disconnectCount == 0,
        "no disconnect at all when keepAliveTimeout == 0 and client is silent");

    // Stop the server; the client will be cleaned up in run()'s final loop.
    server.requestStop();
    serverThread.join();

    // onDisconnect must be called for the client in the cleanup sweep.
    REQUIRE_MSG(server.disconnectCount == 1,
        "onDisconnect called for surviving client on server shutdown");
    // But timeoutClosedCount must remain 0 — the cleanup loop doesn't call
    // onClientsTimedOut; it is not a timeout close, it is a shutdown close.
    REQUIRE_MSG(server.timeoutClosedCount == 0,
        "onClientsTimedOut never called when keepAliveTimeout == 0");
}

// ---------------------------------------------------------------------------
// Test 5: only_idle_client_closed
//
// Property: with two clients — one idle, one periodically sending data —
// only the idle client's heap entry should become genuine; the active
// client's entries are continually superseded by touches.  After
// KEEP_ALIVE + GRACE has elapsed, exactly one timeout close must have
// occurred (for the idle client); the active client must still be alive.
// ---------------------------------------------------------------------------
static void test_only_idle_client_closed() {
    BEGIN_TEST("timeout heap: only idle client closed, active client survives");

    // Port 0: OS picks; we read it back after the server constructor has
    // already bound, eliminating the TOCTOU window of pickFreePort().
    TimedServer server(0);
    REQUIRE(server.isValid());
    const uint16_t port = server.actualPort();
    REQUIRE(port != 0);

    std::atomic<bool> serverReady{false};
    std::thread serverThread([&] {
        serverReady = true;
        server.run(ClientLimit::Unlimited, POLL_TICK);
    });

    REQUIRE(waitUntil([&] { return serverReady.load(); }, 2s));
    sleepFor(50ms);

    // Client A: idle — connects but never sends.
    TcpSocket clientA = connectClient(port);
    REQUIRE_MSG(clientA.isValid(), "client A connected");

    // Client B: active — sends a byte every 250ms so its timer is refreshed
    //           well within each 1s keep-alive window.
    TcpSocket clientB = connectClient(port);
    REQUIRE_MSG(clientB.isValid(), "client B connected");

    static constexpr auto touchInterval = 250ms;

    // Background thread keeps B alive by sending periodically.
    std::atomic<bool> stopTouching{false};
    std::thread touchThread([&] {
        while (!stopTouching.load()) {
            sleepFor(touchInterval);
            if (stopTouching.load()) break;
            const char t[] = "t";
            clientB.send(t, 1); // may silently fail if already disconnected
        }
    });

    // Wait long enough for A to time out but B to have been touched 4+ times.
    sleepFor(KEEP_ALIVE + GRACE);

    // --- assertion: exactly one timeout close (for A) ---
    REQUIRE_MSG(server.timeoutClosedCount == 1,
        "exactly one timeout close — idle client A timed out");

    // Stop the background touch thread.
    stopTouching = true;
    touchThread.join();

    // Stop the server; B is still connected and will be cleaned up.
    server.requestStop();
    serverThread.join();

    // After server stops:
    //   disconnectCount = 2  (A via timeout, B via server shutdown)
    //   timeoutClosedCount = 1  (only A)
    REQUIRE_MSG(server.disconnectCount == 2,
        "total disconnect count is 2 (A by timeout, B by shutdown)");
    REQUIRE_MSG(server.timeoutClosedCount == 1,
        "B was NOT closed by the timeout mechanism");
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
int main() {
    std::cout << "=== Timeout Heap Provability Tests ===\n";

    test_timeout_fires_when_idle();
    test_touch_resets_expiry();
    test_multiple_touches_no_spurious_close();
    test_zero_timeout_disables_sweep();
    test_only_idle_client_closed();

    return test_summary();
}
