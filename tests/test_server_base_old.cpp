// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Tests for ServerBase<T>:
//   1. requestStop() from another thread causes run() to return cleanly.
//   2. Server exits when maxClients limit is reached and all clients leave.
//   3. onIdle() is called periodically when a bounded timeout is used.
//   4. onDisconnect() is called for each client when the server stops.

#include "ServerBase.h"
#include "TcpSocket.h"
#include "test_helpers.h"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

using namespace aiSocks;

// ---------------------------------------------------------------------------
// Minimal echo server for tests
// ---------------------------------------------------------------------------
struct EchoState {
    std::string buf;
    bool disconnected{false};
    bool hasSentData{false}; // Track if we've sent data back
};

class EchoServer : public ServerBase<EchoState> {
    public:
    explicit EchoServer(uint16_t port)
        : ServerBase<EchoState>(ServerBind{"127.0.0.1", Port{port}, 5}) {
        setKeepAliveTimeout(std::chrono::milliseconds{1000});
    }

    std::atomic<int> idleCalls{0};
    std::atomic<int> disconnectCalls{0};

    // Get the actual port the server is listening on
    uint16_t getActualPort() const {
        return getSocket().getLocalEndpoint()->port;
    }

    protected:
    ServerResult onReadable(TcpSocket& sock, EchoState& s) override {
        char tmp[1024];
        for (;;) {
            int n = sock.receive(tmp, sizeof(tmp));
            if (n > 0) {
                touchClient(sock);
                s.buf.append(tmp, static_cast<size_t>(n));
            } else if (n == 0) {
                return ServerResult::Disconnect; // peer closed
            } else {
                const auto err = sock.getLastError();
                if (err == SocketError::WouldBlock
                    || err == SocketError::Timeout)
                    break;
                return ServerResult::Disconnect;
            }
        }
        return ServerResult::KeepConnection;
    }

    ServerResult onWritable(TcpSocket& sock, EchoState& s) override {
        if (s.buf.empty()) return ServerResult::KeepConnection;
        int n = sock.send(s.buf.data(), s.buf.size());
        if (n > 0) {
            touchClient(sock);
            s.buf.erase(0, static_cast<size_t>(n));
            s.hasSentData = true; // Mark that we've sent data
        }
        // If buffer is empty after sending and we've sent data, close
        // connection
        if (s.buf.empty() && s.hasSentData) return ServerResult::Disconnect;
        return ServerResult::KeepConnection;
    }

    ServerResult onDisconnect(EchoState& s) override {
        s.disconnected = true;
        ++disconnectCalls;
        return ServerResult::KeepConnection;
    }

    ServerResult onIdle() override {
        ++idleCalls;
        return ServerResult::KeepConnection;
    }
};

// ---------------------------------------------------------------------------
// Helper: connect a client, send a message, receive echo, disconnect
// ---------------------------------------------------------------------------
static void runClient(uint16_t port, const std::string& msg) {
    try {
        TcpSocket sock(
            AddressFamily::IPv4, ConnectArgs{"127.0.0.1", Port{port}});
        if (!sock.setBlocking(false)) return; // Should not fail in test
        sock.sendAll(msg.data(), msg.size());
        // drain until peer closes or would block
        char buf[256];
        while (true) {
            int n = sock.receive(buf, sizeof(buf));
            if (n <= 0) break;
        }
    } catch (...) {
    }
}

// ---------------------------------------------------------------------------
// Test 1: requestStop() causes run() to exit cleanly from another thread.
// ---------------------------------------------------------------------------
static void testRequestStop() {
    BEGIN_TEST("ServerBase: requestStop() causes clean shutdown");

    EchoServer srv(0); // Use port 0 to let OS pick free port
    uint16_t actualPort = srv.getActualPort();

    std::thread srvThread([&]() {
        // Use a 50ms timeout so the loop wakes frequently.
        srv.run(0, Milliseconds{50});
    });

    // Give the server a moment to enter the poll loop.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    REQUIRE(!srv.stopRequested());

    srv.requestStop();

    // Server should exit within a few hundred ms.
    auto deadline
        = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    srvThread.join();
    REQUIRE(std::chrono::steady_clock::now() < deadline);

    REQUIRE(srv.stopRequested());
}

// ---------------------------------------------------------------------------
// Test 2: Server exits once maxClients have been served and all disconnect.
// ---------------------------------------------------------------------------
static void testMaxClients() {
    BEGIN_TEST("ServerBase: exits after maxClients disconnect");

    EchoServer srv(0); // Use port 0 to let OS pick free port
    uint16_t actualPort = srv.getActualPort();

    std::thread srvThread(
        [&]() { srv.run(/*maxClients=*/2, Milliseconds{50}); });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Connect two clients, each sends a message then closes.
    std::thread c1(runClient, actualPort, "hello");
    std::thread c2(runClient, actualPort, "world");
    c1.join();
    c2.join();

    // Server should see 2 clients disconnect and exit naturally.
    auto deadline
        = std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);
    srvThread.join();
    REQUIRE(std::chrono::steady_clock::now() < deadline);
    REQUIRE(srv.disconnectCalls.load() == 2);
}

// ---------------------------------------------------------------------------
// Test 3: onIdle() is invoked on every loop iteration.
// ---------------------------------------------------------------------------
static void testOnIdle() {
    BEGIN_TEST("ServerBase: onIdle() is called periodically");

    EchoServer srv(0); // Use port 0 to let OS pick free port

    std::thread srvThread([&]() {
        srv.run(0, Milliseconds{20}); // 20ms timeout -> frequent idle
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    srv.requestStop();
    srvThread.join();

    // With a 20ms timeout over ~200ms we expect at least 5 idle calls.
    REQUIRE(srv.idleCalls.load() >= 5);
}

// ---------------------------------------------------------------------------
// Test 4: onDisconnect() is called for every connected client when server
//         stops mid-flight (requestStop while clients are still alive).
// ---------------------------------------------------------------------------
static void testDisconnectCalledOnStop() {
    BEGIN_TEST("ServerBase: onDisconnect() called for all clients on stop");

    EchoServer srv(0); // Use port 0 to let OS pick free port
    uint16_t actualPort = srv.getActualPort();

    // Connect clients that hold the connection open (send nothing).
    TcpSocket c1(
        AddressFamily::IPv4, ConnectArgs{"127.0.0.1", Port{actualPort}});
    TcpSocket c2(
        AddressFamily::IPv4, ConnectArgs{"127.0.0.1", Port{actualPort}});

    std::thread srvThread([&]() { srv.run(0, Milliseconds{50}); });

    // Wait until both clients are accepted.
    auto deadline
        = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
    while (
        srv.clientCount() < 2 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(srv.clientCount() == 2);

    srv.requestStop();
    srvThread.join();

    // Both clients should have triggered onDisconnect.
    REQUIRE(srv.disconnectCalls.load() == 2);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    std::cout << "=== ServerBase Tests ===\n";

    testRequestStop();
    testMaxClients();
    testOnIdle();
    testDisconnectCalledOnStop();

    return test_summary();
}
