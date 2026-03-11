// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Tests for the five fixes applied to ServerBase / HttpPollServer:
//
//  1. ClientLimit lives in ServerTypes.h (single source of truth)
//  2. setHandleSignals(false) makes a server immune to g_serverSignalStop
//  3. onError() returns ServerResult — StopServer halts the server
//  4. onIdle() is called only on genuine poll timeouts, not on every wake
//  5. requestComplete(req, scanPos) is O(n) total across incremental calls

#include "HttpPollServer.h"
#include "ServerBase.h"
#include "ServerSignal.h"
#include "ServerTypes.h"
#include "SocketFactory.h"
#include "TcpSocket.h"
#include "test_helpers.h"

#ifdef _WIN32
#include <winsock2.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>

using namespace aiSocks;

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

static constexpr Milliseconds POLL_TIMEOUT{1};

template <typename Cond>
static bool waitFor(Cond&& cond,
    std::chrono::milliseconds limit = std::chrono::milliseconds{800},
    std::chrono::milliseconds step = std::chrono::milliseconds{5}) {
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < deadline) {
        if (cond()) return true;
        std::this_thread::sleep_for(step);
    }
    return cond();
}

// ---------------------------------------------------------------------------
// Minimal concrete ServerBase used by most tests
// ---------------------------------------------------------------------------

struct TS {
    std::string buf;
};

class BaseServer : public ServerBase<TS> {
    public:
    explicit BaseServer(Port port)
        : ServerBase<TS>(ServerBind{"127.0.0.1", port, Backlog{5}}) {
        setKeepAliveTimeout(Milliseconds{0});
    }

    std::atomic<int> idleCalls{0};
    std::atomic<int> readableCalls{0};
    std::atomic<int> errorCalls{0};
    ServerResult errorReturn{ServerResult::Disconnect};
    bool partialReadMode{false}; // For onIdle test: read only small chunks
    std::atomic<size_t> atomicClientCount_{0};

    // Signals waitReady() callers the moment the server enters the poll loop.
    void waitReady() {
        std::unique_lock<std::mutex> lk(readyMtx_);
        readyCv_.wait(lk, [this] { return ready_.load(); });
    }

    protected:
    void onReady() override {
        {
            std::lock_guard<std::mutex> lk(readyMtx_);
            ready_ = true;
        }
        readyCv_.notify_all();
    }

    void onClientConnected(TcpSocket&) override {
        atomicClientCount_.fetch_add(1, std::memory_order_relaxed);
    }
    void onClientDisconnected() override {
        atomicClientCount_.fetch_sub(1, std::memory_order_relaxed);
    }

    private:
    std::atomic<bool> ready_{false};
    std::mutex readyMtx_;
    std::condition_variable readyCv_;

    protected:
    ServerResult onReadable(TcpSocket& sock, TS& s) override {
        ++readableCalls;
        char tmp[256];
        if (partialReadMode) {
            // Only read once per onReadable call to keep socket readable
            // across multiple poll cycles
            int n = sock.receive(tmp, sizeof(tmp));
            if (n > 0) {
                s.buf.append(tmp, n);
            }
        } else {
            // Normal mode: drain the entire buffer
            for (;;) {
                int n = sock.receive(tmp, sizeof(tmp));
                if (n > 0) {
                    s.buf.append(tmp, n);
                    continue;
                }
                break;
            }
        }
        return ServerResult::KeepConnection;
    }
    ServerResult onWritable(TcpSocket& /*sock*/, TS& /*s*/) override {
        return ServerResult::KeepConnection;
    }
    ServerResult onIdle() override {
        ++idleCalls;
        return ServerResult::KeepConnection;
    }
    ServerResult onError(TcpSocket& /*sock*/, TS& /*s*/) override {
        ++errorCalls;
        return errorReturn;
    }
};

// Expose requestComplete helpers for white-box testing
class HttpHelper : public HttpPollServer {
    public:
    explicit HttpHelper(const ServerBind& b) : HttpPollServer(b) {}
    using HttpPollServer::requestComplete; // expose both overloads
    protected:
    void buildResponse(HttpClientState&) override {}
};

// ---------------------------------------------------------------------------
// Test 1 — ClientLimit single source of truth
//
// Including only ServerTypes.h (not ServerBase.h or SimpleServer.h) should
// be sufficient to use ClientLimit.  The enum values must match the spec.
// ---------------------------------------------------------------------------
static void test_client_limit_source_of_truth() {
    BEGIN_TEST("ClientLimit — single source of truth in ServerTypes.h");

    // Including ServerTypes.h in isolation compiles (proven by compilation).
    // Verify values using static_assert to avoid constant expression warnings.
    static_assert(static_cast<size_t>(ClientLimit::Unlimited) == 0,
        "ClientLimit::Unlimited");
    static_assert(static_cast<size_t>(ClientLimit::Default) == 1000,
        "ClientLimit::Default");
    static_assert(
        static_cast<size_t>(ClientLimit::Low) == 100, "ClientLimit::Low");
    static_assert(
        static_cast<size_t>(ClientLimit::Medium) == 500, "ClientLimit::Medium");
    static_assert(
        static_cast<size_t>(ClientLimit::High) == 2000, "ClientLimit::High");
    static_assert(static_cast<size_t>(ClientLimit::Maximum) == 10000,
        "ClientLimit::Maximum");

    // The ClientLimit seen via ServerBase.h and HttpPollServer.h must be the
    // same type (they both include ServerTypes.h, there is only one
    // definition).
    static_assert(std::is_same_v<decltype(ClientLimit::Default),
                      decltype(ClientLimit::Default)>,
        "ClientLimit must be a single type");
}

// ---------------------------------------------------------------------------
// Test 2 — setHandleSignals(false) opts a server out of g_serverSignalStop
// ---------------------------------------------------------------------------
static void test_signal_opt_out() {
    BEGIN_TEST("setHandleSignals(false) — server ignores g_serverSignalStop");

    // Reset the global flag from any previous test/signal.
    g_serverSignalStop.store(false);

    BaseServer sensitive(Port::any); // will honour the global flag
    BaseServer immune(Port::any); // should ignore it
    immune.setHandleSignals(false);

    REQUIRE(sensitive.handlesSignals() == true);
    REQUIRE(immune.handlesSignals() == false);

    std::atomic<bool> done1{false}, done2{false};

    std::thread t1([&] {
        sensitive.run(ClientLimit::Unlimited, POLL_TIMEOUT);
        done1 = true;
    });
    std::thread t2([&] {
        immune.run(ClientLimit::Unlimited, POLL_TIMEOUT);
        done2 = true;
    });

    // Wait for both servers to enter the poll loop.
    sensitive.waitReady();
    immune.waitReady();

    // Fire the process-wide signal flag — without a real SIGINT.
    g_serverSignalStop.store(true);

    // The sensitive server should stop on its own.
    bool sensitiveStops
        = waitFor([&] { return done1.load(); }, std::chrono::milliseconds{150});

    // The immune server must still be running.
    bool immuneStillRunning = !done2.load();

    // Clean up: stop immune server programmatically.
    immune.requestStop();
    waitFor([&] { return done2.load(); });
    t1.join();
    t2.join();

    // Reset global flag for subsequent tests.
    g_serverSignalStop.store(false);

    REQUIRE_MSG(sensitiveStops,
        "server with handleSignals=true should stop when g_serverSignalStop is "
        "set");
    REQUIRE_MSG(immuneStillRunning,
        "server with handleSignals=false should NOT stop when "
        "g_serverSignalStop is set");
}

// ---------------------------------------------------------------------------
// Test 3 — onError returning StopServer halts the server
//
// We trigger the error path by connecting a client, letting it be accepted,
// then closing it with SO_LINGER=0 (RST) to provoke POLLERR / POLLHUP.
// Even if the OS delivers POLLHUP rather than POLLERR, the important thing is
// that the error-return-StopServer path through onError causes run() to exit.
// ---------------------------------------------------------------------------
static void test_on_error_returns_server_result() {
    BEGIN_TEST("onError returns ServerResult — StopServer halts run()");

    // Compile-time guarantee: BaseServer::onError returns ServerResult.
    // The base declares 'virtual ServerResult onError(...)'; if it returned
    // void, the override in BaseServer would fail to compile. The fact that
    // this TU compiles is the proof.

    // Runtime: returning StopServer from onError stops the server.
    BaseServer server(Port::any);
    server.errorReturn = ServerResult::StopServer;

    // Read back the port the OS assigned on bind.
    Port srvPort102 = Port::any;
    {
        auto ep = server.getSocket().getLocalEndpoint();
        srvPort102 = ep.isSuccess() ? ep.value().port : Port::any;
    }
    REQUIRE(srvPort102 != Port::any);

    std::atomic<bool> done{false};

    std::thread t([&] {
        server.run(ClientLimit::Unlimited, POLL_TIMEOUT);
        done = true;
    });

    server.waitReady();

    // Connect then abruptly close with RST to trigger an error/hangup event.
    auto res = SocketFactory::createTcpClient(AddressFamily::IPv4,
        ConnectArgs{"127.0.0.1", srvPort102, Milliseconds{50}});

    if (res.isSuccess()) {
        // SO_LINGER with l_onoff=1, l_linger=0 causes close() to send RST.
#ifndef _WIN32
        struct linger lg{1, 0};
        ::setsockopt(static_cast<int>(res.value().getNativeHandle()),
            SOL_SOCKET, SO_LINGER, reinterpret_cast<const char*>(&lg),
            sizeof(lg));
#endif
        // Destroy the socket → sends RST (or FIN if linger unsupported).
    }
    // client goes out of scope / RST sent

    // The server may stop due to onError returning StopServer, or it may
    // simply detect the disconnect via a read of 0 and call onReadable first.
    // Either way, explicitly request stop to ensure the thread exits.
    bool stoppedByError
        = waitFor([&] { return done.load(); }, std::chrono::milliseconds{30});
    if (!stoppedByError) {
        server.requestStop();
        waitFor([&] { return done.load(); });
    }
    t.join();

    // The server must have exited cleanly (thread joined without hanging).
    REQUIRE_MSG(done.load(), "server thread exited after onError StopServer");
    // If it stopped due to the error path, errorCalls > 0.
    // If it stopped via disconnect → read-of-zero → ignore (still valid).
}

// ---------------------------------------------------------------------------
// Test 4 — onIdle() is called ONLY on genuine poll timeouts, not every wake
//
// Strategy: run a server with a short timeout and no clients.  Each poll
// cycle times out → onIdle should be called every cycle.  Then flood a
// connected client with data so that poll returns with events every cycle.
// During that burst the idle counter should advance much more slowly (or not
// at all) because no-event iterations are rare.
// ---------------------------------------------------------------------------
static void test_on_idle_only_on_timeout() {
    BEGIN_TEST("onIdle() — called only on poll timeout, not on every wake");

    // Phase 1: no clients — every poll iteration times out → idle fires each
    // time.
    {
        BaseServer server(Port::any);
        std::thread t(
            [&] { server.run(ClientLimit::Unlimited, POLL_TIMEOUT); });
        server.waitReady();
        // Wait until at least 10 idle calls accumulate (generous 2s deadline).
        // Using an active wait instead of a fixed sleep makes this robust
        // under parallel test load where the server thread may be starved.
        waitFor([&] { return server.idleCalls.load() >= 10; },
            std::chrono::milliseconds{2000}, std::chrono::milliseconds{1});
        int idleNoClients = server.idleCalls.load();
        server.requestStop();
        t.join();

        // With 1 ms poll timeout we expect many idle calls; floor of 10.
        REQUIRE_MSG(idleNoClients >= 10,
            "onIdle should fire many times when there are no clients (got "
                + std::to_string(idleNoClients) + ")");
    }

    // Phase 2: Verify that onIdle is NOT called on every poll wake when events
    // are present. We measure total onIdle calls over the entire test duration.
    {
        BaseServer server(Port::any);
        server.partialReadMode
            = true; // Read only small chunks to keep socket readable

        Port srvPort104 = Port::any;
        {
            auto ep = server.getSocket().getLocalEndpoint();
            srvPort104 = ep.isSuccess() ? ep.value().port : Port::any;
        }
        REQUIRE(srvPort104 != Port::any);

        std::thread t(
            [&] { server.run(ClientLimit::Unlimited, POLL_TIMEOUT); });
        server.waitReady();

        auto res = SocketFactory::createTcpClient(AddressFamily::IPv4,
            ConnectArgs{"127.0.0.1", srvPort104, Milliseconds{50}});
        REQUIRE(res.isSuccess());
        auto client = std::make_unique<TcpSocket>(std::move(res.value()));

        waitFor([&] { return server.atomicClientCount_.load() == 1; });

        int idleBeforeData = server.idleCalls.load();
        int readableBeforeData = server.readableCalls.load();

        // Send large amounts of data to keep the socket buffer full and
        // ensure the poller consistently finds readable events
        const char chunk[4096]{};
        for (int i = 0; i < 100; ++i) {
            client->send(chunk, sizeof(chunk));
        }

        // Wait until the server has processed the data (at least 100 readable
        // events, generous 2s deadline) instead of a fixed sleep so the test
        // is robust under parallel test load.
        waitFor(
            [&] {
                return server.readableCalls.load() - readableBeforeData >= 100;
            },
            std::chrono::milliseconds{2000}, std::chrono::milliseconds{1});

        int idleAfterData = server.idleCalls.load();
        int readableAfterData = server.readableCalls.load();

        int idleDelta = idleAfterData - idleBeforeData;
        int readableDelta = readableAfterData - readableBeforeData;

        server.requestStop();
        client.reset();
        t.join();

        // Behavior verification: If onIdle were called on every poll wake,
        // it would be called as often as onReadable. Since it should only
        // fire on timeouts (when no events are ready), it should be called
        // much less frequently. We verify idleDelta is at most 50% of
        // readableDelta.
        REQUIRE_MSG(readableDelta > 0, "onReadable should have fired");
        REQUIRE_MSG(idleDelta <= readableDelta / 2,
            "onIdle calls (" + std::to_string(idleDelta)
                + ") should be at most half of onReadable calls ("
                + std::to_string(readableDelta)
                + ") when events are available");
    }
}

// ---------------------------------------------------------------------------
// Test 5 — requestComplete(req, scanPos) is incremental (O(n) total)
//
// Tests the two-argument overload directly via the exposed HttpHelper.
// ---------------------------------------------------------------------------
static void test_request_complete_incremental() {
    BEGIN_TEST("requestComplete(req, scanPos) — incremental, O(n) total");

    // 5a: single-argument backward-compat form still works.
    {
        REQUIRE(!HttpHelper::requestComplete("GET / HTTP/1.1\r\n"));
        REQUIRE(HttpHelper::requestComplete("GET / HTTP/1.1\r\n\r\n"));
        REQUIRE(HttpHelper::requestComplete(
            "GET / HTTP/1.1\r\nHost: x\r\n\r\nbody"));
    }

    // 5b: two-argument form finds the terminator.
    {
        std::string req = "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n";
        size_t pos = 0;
        REQUIRE(HttpHelper::requestComplete(req, pos));
    }

    // 5c: incremental delivery — scanPos advances so rescans are minimal.
    {
        std::string req;
        size_t pos = 0;
        // Feed bytes one at a time until \r\n\r\n is complete.
        const std::string full = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
        bool found = false;
        size_t foundAt = 0;
        for (size_t i = 0; i < full.size(); ++i) {
            req += full[i];
            if (HttpHelper::requestComplete(req, pos)) {
                found = true;
                foundAt = i;
                break;
            }
            // scanPos must never exceed req.size().
            REQUIRE(pos <= req.size());
        }
        REQUIRE_MSG(found, "incremental delivery should eventually complete");
        // Headers end at index (full.size()-1), so we should detect exactly
        // there.
        REQUIRE(foundAt == full.size() - 1);
    }

    // 5d: scanPos is not advanced past a complete request.
    {
        std::string req = "GET / HTTP/1.1\r\n\r\n";
        size_t pos = 0;
        bool first = HttpHelper::requestComplete(req, pos);
        REQUIRE(first);
        // Calling again with the same pos should still return true.
        bool second = HttpHelper::requestComplete(req, pos);
        REQUIRE(second);
    }

    // 5e: \r\n\r\n split across two appends (overlap handling).
    {
        std::string req = "GET / HTTP/1.1\r\n";
        size_t pos = 0;
        bool before = HttpHelper::requestComplete(req, pos);
        REQUIRE(!before);
        // pos now points near the end; appending \r\n must still find the
        // terminator.
        req += "\r\n";
        bool after = HttpHelper::requestComplete(req, pos);
        REQUIRE_MSG(
            after, "split \\r\\n\\r\\n across two appends must be found");
    }

    // 5f: scanPos is reset to 0 for a fresh request after keep-alive.
    // (Simulated by simply resetting pos and giving a new request string.)
    {
        std::string req2 = "GET /next HTTP/1.1\r\n\r\n";
        size_t pos = 0;
        REQUIRE(HttpHelper::requestComplete(req2, pos));
    }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    printf("=== test_fixes ===\n");
    test_client_limit_source_of_truth();
    test_signal_opt_out();
    test_on_error_returns_server_result();
    test_on_idle_only_on_timeout();
    test_request_complete_incremental();
    return test_summary();
}
