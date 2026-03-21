// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

//
// test_simple_server.cpp  Tests for the library's aiSocks::SimpleServer class.
//
// Covers:
//   1. isValid() on good and bad bind
//   2. pollClients() — Readable callback fires, echo works, requestStop exits
//   3. pollClients() — callback returning false disconnects the client;
//                      clientCount() drops to 0
//   4. acceptClients() — synchronous per-client callback invoked for each
//                        accepted connection up to the given limit

#include "SimpleServer.h"
#include "SocketFactory.h"
#include "TcpSocket.h"
#include "test_helpers.h"
#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>

// Access global test counters from test_helpers.h
extern std::atomic<int> g_passed;
extern std::atomic<int> g_failed;

using namespace aiSocks;

// Helper: busy-wait with timeout until condition becomes true.
template <typename Cond>
static bool waitFor(Cond&& cond,
    std::chrono::milliseconds limit = std::chrono::milliseconds{3000},
    std::chrono::milliseconds step = std::chrono::milliseconds{10}) {
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (!cond()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(step);
    }
    return true;
}

// Read back the OS-assigned port after binding with Port::any.
static Port serverPort(const SimpleServer& s) {
    auto ep = s.getSocket().getLocalEndpoint();
    return ep.isSuccess() ? ep.value().port : Port::any;
}

// SimpleServer subclass that maintains a thread-safe client count via the
// onClientConnected / onClientDisconnected hooks (both called on the server
// thread, but readable from any thread via the atomic).
class TrackingServer : public SimpleServer {
    public:
    explicit TrackingServer(
        const ServerBind& args, AddressFamily family = AddressFamily::IPv4)
        : SimpleServer(args, family) {}

    std::atomic<size_t> atomicClientCount_{0};

    protected:
    void onClientConnected(
        TcpSocket& /*sock*/, detail::NoClientState& /*s*/) override {
        atomicClientCount_.fetch_add(1, std::memory_order_relaxed);
    }
    void onClientDisconnected() override {
        atomicClientCount_.fetch_sub(1, std::memory_order_relaxed);
    }
};

// -----------------------------------------------------------------------
// 1. Validity
// -----------------------------------------------------------------------
static void test_validity() {
    BEGIN_TEST("SimpleServer: isValid() true on valid bind");
    {
        SimpleServer s(ServerBind{"127.0.0.1", Port::any, ""});
        REQUIRE(s.isValid());
        REQUIRE(serverPort(s) != Port::any); // OS chose a real port
    }

    BEGIN_TEST("SimpleServer: isValid() false on bad address");
    {
        // Test bind failure directly without fatal exit
        auto result = SocketFactory::createTcpServer(
            AddressFamily::IPv4, ServerBind{"999.999.999.999", Port::any, ""});
        REQUIRE(result.isError());
    }
}

// -----------------------------------------------------------------------
// 2. pollClients: Readable fires, echo works, requestStop exits the loop
// -----------------------------------------------------------------------
static void test_poll_clients_echo() {
    BEGIN_TEST(
        "SimpleServer: pollClients Readable callback fires and can echo");

    const std::string msg = "hello-simple-server";
    std::string echoed;
    std::atomic<bool> serverReady{false};

    SimpleServer server(ServerBind{"127.0.0.1", Port::any, Backlog{5, ""}});
    REQUIRE(server.isValid());
    Port port = serverPort(server);
    REQUIRE(port != Port::any);
    server.setHandleSignals(false);
    server.setKeepAliveTimeout(Milliseconds{0});

    std::thread serverThread([&] {
        serverReady.store(true);
        server.pollClients(
            [&](TcpSocket& sock, PollEvent ev) -> bool {
                if (hasFlag(ev, PollEvent::Readable)) {
                    char buf[256] = {};
                    int n = sock.receive(buf, sizeof(buf));
                    if (n > 0) {
                        sock.send(buf, n); // echo back
                        server.requestStop();
                    } else if (n < 0
                        && sock.getLastError() != SocketError::WouldBlock) {
                        return false;
                    }
                }
                return true;
            },
            ClientLimit{1}, Milliseconds{1});
    });

    REQUIRE(waitFor([&] { return serverReady.load(); }));

    auto res = SocketFactory::createTcpClient(AddressFamily::IPv4,
        ConnectArgs{"127.0.0.1", port, Milliseconds{1000}});
    REQUIRE(res.isSuccess());
    auto client = std::make_unique<TcpSocket>(std::move(res.value()));

    int sent = client->send(msg.data(), msg.size());
    REQUIRE(sent == static_cast<int>(msg.size()));

    // Blocking socket: receive() will block until data arrives.
    char rbuf[256] = {};
    int received = client->receive(rbuf, sizeof(rbuf));
    REQUIRE(received == static_cast<int>(msg.size()));
    REQUIRE(std::string(rbuf, received) == msg);

    client.reset();
    serverThread.join();
}

// -----------------------------------------------------------------------
// 3. pollClients: returning false from callback disconnects the client;
//    clientCount() falls to 0 before the server is stopped.
// -----------------------------------------------------------------------
static void test_poll_clients_disconnect_on_false() {
    BEGIN_TEST("SimpleServer: callback returning false disconnects client");

    std::atomic<bool> serverReady{false};
    std::atomic<bool> callbackFired{false};

    TrackingServer server(ServerBind{"127.0.0.1", Port::any, Backlog{5, ""}});
    REQUIRE(server.isValid());
    Port port = serverPort(server);
    REQUIRE(port != Port::any);
    server.setHandleSignals(false);
    server.setKeepAliveTimeout(Milliseconds{0});

    std::thread serverThread([&] {
        serverReady.store(true);
        server.pollClients(
            [&](TcpSocket& sock, PollEvent ev) -> bool {
                if (hasFlag(ev, PollEvent::Readable)) {
                    char buf[64] = {};
                    sock.receive(buf, sizeof(buf)); // drain
                    callbackFired.store(true);
                    return false; // signal: disconnect this client
                }
                return true;
            },
            ClientLimit::Unlimited, Milliseconds{1});
    });

    REQUIRE(waitFor([&] { return serverReady.load(); }));

    auto res = SocketFactory::createTcpClient(AddressFamily::IPv4,
        ConnectArgs{"127.0.0.1", port, Milliseconds{1000}});
    REQUIRE(res.isSuccess());
    auto client = std::make_unique<TcpSocket>(std::move(res.value()));

    client->send("trigger", 7);

    REQUIRE(waitFor([&] { return callbackFired.load(); }));
    // Wait for onClientDisconnected to fire — safe cross-thread via atomic.
    REQUIRE(waitFor([&] { return server.atomicClientCount_.load() == 0; }));

    server.requestStop();
    client.reset();
    serverThread.join();

    REQUIRE(server.atomicClientCount_.load() == 0);
}

// -----------------------------------------------------------------------
// 4. acceptClients: callback invoked once per accepted connection,
//    exactly up to the given ClientLimit.
// -----------------------------------------------------------------------
static void test_accept_clients() {
    BEGIN_TEST(
        "SimpleServer: acceptClients invokes callback for each connection");

    std::atomic<int> callbackCount{0};
    std::atomic<bool> serverReady{false};

    SimpleServer server(ServerBind{"127.0.0.1", Port::any, Backlog{5, ""}});
    REQUIRE(server.isValid());
    Port port = serverPort(server);
    REQUIRE(port != Port::any);
    server.setHandleSignals(false);

    std::thread serverThread([&] {
        serverReady.store(true);
        server.acceptClients(
            [&](TcpSocket& sock) {
                // acceptClients sets the socket non-blocking. Use receiveAll
                // to handle EAGAIN gracefully while echoing.
                char buf[64] = {};
                if (sock.receiveAll(buf, 4)) {
                    sock.sendAll(buf, 4); // echo
                }
                ++callbackCount;
            },
            ClientLimit{2});
    });

    REQUIRE(waitFor([&] { return serverReady.load(); }));

    // Connect two clients sequentially; wait for each callback to complete
    // before connecting the next one (acceptClients is synchronous).
    for (int i = 0; i < 2; ++i) {
        auto res = SocketFactory::createTcpClient(AddressFamily::IPv4,
            ConnectArgs{"127.0.0.1", port, Milliseconds{1000}});
        REQUIRE(res.isSuccess());
        {
            auto c = std::make_unique<TcpSocket>(std::move(res.value()));
            const char* ping = "ping";
            c->send(ping, 4);
            // Wait for the echo to confirm the callback executed.
            char buf[8] = {};
            c->receive(buf, sizeof(buf));
        } // c closes here

        REQUIRE(waitFor([&] { return callbackCount > i; }));
    }

    serverThread.join();
    REQUIRE(callbackCount == 2);
}

int main() {
    printf("=== SimpleServer Tests ===\n\n");

    test_validity();
    test_poll_clients_echo();
    test_poll_clients_disconnect_on_false();
    test_accept_clients();

    return test_summary();
}
