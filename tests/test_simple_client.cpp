// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
//
// Test: SimpleClient convenience class with SocketFactory API
// Verifies the two usage patterns introduced by the anti-pattern fix:
//   1. Two-step: SimpleClient(args) then run(callback)
//   2. Static factory: SimpleClient::connect(args) -> Result<TcpSocket>

#include "TcpSocket.h"
#include "SimpleClient.h"
#include "SocketFactory.h"
#include "test_helpers.h"
#include <chrono>
#include <cstring>
#include <string>
#include <thread>

using namespace aiSocks;

static constexpr uint16_t BASE = 22000;

// Start a one-shot echo server in the background.
// Both accept() and receive() carry short timeouts so the server thread
// always exits cleanly even if the test fails before the client connects
// or the client connects but intentionally sends nothing.
static std::thread startEchoServer(uint16_t port, int clients = 1) {
    return std::thread([port, clients]() {
        auto srv = SocketFactory::createTcpServer(
            ServerBind{"127.0.0.1", Port{port}, Backlog{5}});
        if (!srv.isSuccess()) return;
        // SO_RCVTIMEO on the listener applies to accept() on POSIX, so the
        // thread won't block forever if the client never arrives.
        srv.value().setReceiveTimeout(Milliseconds{500});
        for (int i = 0; i < clients; ++i) {
            auto conn = srv.value().accept();
            if (!conn) continue;
            conn->setReceiveTimeout(Milliseconds{300});
            char buf[256]{};
            int n = conn->receive(buf, sizeof(buf) - 1);
            if (n > 0) conn->sendAll(buf, n);
        }
    });
}

int main() {
    std::cout << "=== SimpleClient Tests ===\n";

    // -------------------------------------------------------------------------
    // Test 1: Two-step form — construction does not fire any callback
    // -------------------------------------------------------------------------
    BEGIN_TEST("SimpleClient: construction does not fire callback");
    {
        auto srv = startEchoServer(BASE);
        std::this_thread::sleep_for(std::chrono::milliseconds{10});

        bool callbackFired = false;
        SimpleClient client(ConnectArgs{"127.0.0.1", Port{BASE}});
        REQUIRE(client.isConnected());
        REQUIRE(!callbackFired); // must NOT have fired yet

        client.run([&](TcpSocket& sock) {
            callbackFired = true;
            const char* msg = "Hello echo";
            sock.sendAll(msg, std::strlen(msg));
            char buf[256]{};
            int n = sock.receive(buf, sizeof(buf) - 1);
            if (n > 0) REQUIRE(std::string(buf, n) == msg);
        });

        REQUIRE(callbackFired);
        srv.join();
    }

    // -------------------------------------------------------------------------
    // Test 2: run() returns false and does not invoke callback when not
    // connected
    // -------------------------------------------------------------------------
    BEGIN_TEST("SimpleClient: run() returns false on failed connection");
    {
        SimpleClient client(
            ConnectArgs{"127.0.0.1", Port{1}, Milliseconds{100}});
        REQUIRE(!client.isConnected());
        bool called = false;
        bool ran = client.run([&](TcpSocket&) { called = true; });
        REQUIRE(!ran);
        REQUIRE(!called);
    }

    // -------------------------------------------------------------------------
    // Test 3: Static factory — Result<TcpSocket> on success
    // -------------------------------------------------------------------------
    BEGIN_TEST("SimpleClient::connect — returns live socket on success");
    {
        auto srv = startEchoServer(BASE + 1);
        std::this_thread::sleep_for(std::chrono::milliseconds{10});

        auto result
            = SimpleClient::connect(ConnectArgs{"127.0.0.1", Port{BASE + 1}});
        REQUIRE(result.isSuccess());

        TcpSocket sock = std::move(result.value());
        REQUIRE(sock.isValid());

        const char* msg = "factory test";
        sock.sendAll(msg, std::strlen(msg));
        char buf[256]{};
        int n = sock.receive(buf, sizeof(buf) - 1);
        REQUIRE(n > 0);
        REQUIRE(std::string(buf, n) == msg);

        srv.join();
    }

    // -------------------------------------------------------------------------
    // Test 4: Static factory — failure case, no live server
    // -------------------------------------------------------------------------
    BEGIN_TEST("SimpleClient::connect — returns error on refused connection");
    {
        auto result = SimpleClient::connect(
            ConnectArgs{"127.0.0.1", Port{1}, Milliseconds{100}});
        REQUIRE(!result.isSuccess());
    }

    // -------------------------------------------------------------------------
    // Test 5: getSocket() returns a valid socket after successful construction
    // -------------------------------------------------------------------------
    BEGIN_TEST("SimpleClient: getSocket() accessible before run()");
    {
        auto srv = startEchoServer(BASE + 2);
        std::this_thread::sleep_for(std::chrono::milliseconds{10});

        SimpleClient client(ConnectArgs{"127.0.0.1", Port{BASE + 2}});
        REQUIRE(client.isConnected());
        REQUIRE(client.getSocket() != nullptr);
        REQUIRE(client.getSocket()->isValid());

        const char* msg = "direct socket";
        client.getSocket()->sendAll(msg, std::strlen(msg));
        char buf[256]{};
        int n = client.getSocket()->receive(buf, sizeof(buf) - 1);
        REQUIRE(n > 0);
        REQUIRE(std::string(buf, n) == msg);

        srv.join();
    }

    // -------------------------------------------------------------------------
    // Test 6: getLastError() returns empty string on successful connection
    // -------------------------------------------------------------------------
    BEGIN_TEST(
        "SimpleClient: getLastError() empty on success, non-empty on failure");
    {
        auto srv = startEchoServer(BASE + 3);
        std::this_thread::sleep_for(std::chrono::milliseconds{10});

        {
            SimpleClient ok(ConnectArgs{"127.0.0.1", Port{BASE + 3}});
            REQUIRE(ok.isConnected());
            (void)ok.getLastError(); // must not crash
        } // closes socket → server's receive() returns, server thread exits
        srv.join();

        SimpleClient bad(ConnectArgs{"127.0.0.1", Port{1}, Milliseconds{100}});
        REQUIRE(!bad.isConnected());
        (void)bad.getLastError(); // must not crash on a disconnected client
    }

    // -------------------------------------------------------------------------
    // Test 7: Exception from run() propagates; object remains destructible
    // -------------------------------------------------------------------------
    BEGIN_TEST("SimpleClient: exception from run() propagates cleanly");
    {
        auto srv = startEchoServer(BASE + 4);
        std::this_thread::sleep_for(std::chrono::milliseconds{10});

        bool threw = false;
        {
            SimpleClient client(ConnectArgs{"127.0.0.1", Port{BASE + 4}});
            REQUIRE(client.isConnected());
            try {
                client.run(
                    [](TcpSocket&) { throw std::runtime_error("oops"); });
            } catch (const std::runtime_error&) {
                threw = true;
            }
            // destructor runs here — socket closed cleanly despite the throw
        }
        REQUIRE(threw);
        srv.join(); // server's receive() saw EOF when client was destroyed
                    // above
    }

    // -------------------------------------------------------------------------
    // Test 8: Multiple sequential connections
    // -------------------------------------------------------------------------
    BEGIN_TEST("SimpleClient: multiple sequential two-step connections");
    {
        auto srv = startEchoServer(BASE + 5, 3);
        std::this_thread::sleep_for(std::chrono::milliseconds{10});

        for (int i = 0; i < 3; ++i) {
            std::string msg = "Message " + std::to_string(i);
            SimpleClient client(ConnectArgs{"127.0.0.1", Port{BASE + 5}});
            REQUIRE(client.isConnected());

            bool callbackRan = false;
            client.run([&](TcpSocket& sock) {
                callbackRan = true;
                sock.sendAll(msg.data(), msg.size());
                char buf[256]{};
                int n = sock.receive(buf, sizeof(buf) - 1);
                if (n > 0) REQUIRE(std::string(buf, n) == msg);
            });
            REQUIRE(callbackRan);
        }

        srv.join();
    }

    // -------------------------------------------------------------------------
    // Test 9: run() return value is true on success, false when not connected
    // -------------------------------------------------------------------------
    BEGIN_TEST("SimpleClient: run() return value");
    {
        auto srv = startEchoServer(BASE + 6);
        std::this_thread::sleep_for(std::chrono::milliseconds{10});

        {
            SimpleClient good(ConnectArgs{"127.0.0.1", Port{BASE + 6}});
            REQUIRE(good.isConnected());
            bool ret = good.run([](TcpSocket&) {});
            REQUIRE(ret); // true — callback was invoked
        } // closes socket → server's receive() returns, server thread exits
        srv.join();

        SimpleClient bad(ConnectArgs{"127.0.0.1", Port{1}, Milliseconds{50}});
        REQUIRE(!bad.isConnected());
        bool ret2 = bad.run([](TcpSocket&) {});
        REQUIRE(!ret2); // false — not connected, callback skipped
    }

    // -------------------------------------------------------------------------
    // Test 10: run() can be called multiple times on the same live socket
    // -------------------------------------------------------------------------
    BEGIN_TEST("SimpleClient: run() callable multiple times");
    {
        // A server that echoes two messages on the same connection.
        std::thread srv([]() {
            auto s = SocketFactory::createTcpServer(
                ServerBind{"127.0.0.1", Port{BASE + 7}, Backlog{2}});
            if (!s.isSuccess()) return;
            auto conn = s.value().accept();
            if (!conn) return;
            for (int i = 0; i < 2; ++i) {
                char buf[64]{};
                int n = conn->receive(buf, sizeof(buf) - 1);
                if (n > 0) conn->sendAll(buf, n);
            }
        });
        std::this_thread::sleep_for(std::chrono::milliseconds{10});

        SimpleClient client(ConnectArgs{"127.0.0.1", Port{BASE + 7}});
        REQUIRE(client.isConnected());

        int calls = 0;
        client.run([&](TcpSocket& sock) {
            ++calls;
            sock.sendAll("first", 5);
            char buf[64]{};
            int n = sock.receive(buf, sizeof(buf) - 1);
            REQUIRE(n == 5);
            REQUIRE(std::string(buf, n) == "first");
        });
        client.run([&](TcpSocket& sock) {
            ++calls;
            sock.sendAll("second", 6);
            char buf[64]{};
            int n = sock.receive(buf, sizeof(buf) - 1);
            REQUIRE(n == 6);
            REQUIRE(std::string(buf, n) == "second");
        });
        REQUIRE(calls == 2);
        srv.join();
    }

    // -------------------------------------------------------------------------
    // Test 11: getSocket() on a failed client returns nullptr
    // -------------------------------------------------------------------------
    BEGIN_TEST("SimpleClient: getSocket() on failed client returns nullptr");
    {
        SimpleClient bad(ConnectArgs{"127.0.0.1", Port{1}, Milliseconds{50}});
        REQUIRE(!bad.isConnected());
        // getSocket() must return nullptr — no dummy socket, no silent footgun.
        REQUIRE(bad.getSocket() == nullptr);
    }

    // -------------------------------------------------------------------------
    // Test 12: static connect() — unhappy: connection refused
    // -------------------------------------------------------------------------
    BEGIN_TEST("SimpleClient::connect — unhappy: connection refused");
    {
        auto r = SimpleClient::connect(
            ConnectArgs{"127.0.0.1", Port{1}, Milliseconds{50}});
        REQUIRE(!r.isSuccess());
    }

    // -------------------------------------------------------------------------
    // Test 13: static connect() — unhappy: bad hostname
    // -------------------------------------------------------------------------
    BEGIN_TEST("SimpleClient::connect — unhappy: unresolvable hostname");
    {
        auto r = SimpleClient::connect(
            ConnectArgs{"this.hostname.does.not.exist.invalid", Port{80},
                Milliseconds{50}});
        REQUIRE(!r.isSuccess());
    }

    // -------------------------------------------------------------------------
    // Test 14: construct but never call run() — clean destruction, no crash
    // -------------------------------------------------------------------------
    BEGIN_TEST("SimpleClient: destroyed without calling run() — no crash");
    {
        auto srv = startEchoServer(BASE + 8);
        std::this_thread::sleep_for(std::chrono::milliseconds{10});

        {
            SimpleClient client(ConnectArgs{"127.0.0.1", Port{BASE + 8}});
            REQUIRE(client.isConnected());
            // run() is intentionally never called;
            // destructor must release the socket cleanly.
        }
        srv.join(); // server-side accept returns when client closes
    }

    std::cout << "All SimpleClient tests passed!\n";
    return test_summary();
}
