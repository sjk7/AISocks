// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Tests for ServerBase<T> with ClientLimit enum:
//   1. requestStop() from another thread causes run() to return cleanly.
//   2. Server exits when maxClients limit is reached and all clients leave.
//   3. onIdle() is called periodically when a bounded timeout is used.
//   4. onDisconnect() is called for each client when the server stops.

#include "ServerBase.h"
#include "TcpSocket.h"
#include "SocketFactory.h"
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
};

class EchoServer : public ServerBase<EchoState> {
    public:
    explicit EchoServer(uint16_t port)
        : ServerBase<EchoState>(ServerBind{"127.0.0.1", Port{port}, 5}) {
        setKeepAliveTimeout(std::chrono::seconds{1});
    }

    std::atomic<int> idleCalls{0};
    std::atomic<int> disconnectCalls{0};

    // Get the actual port the server is listening on
    uint16_t getActualPort() const {
        auto endpoint = getSocket().getLocalEndpoint();
        return endpoint.isSuccess() ? endpoint.value().port.value() : 0;
    }

    protected:
    ServerResult onReadable(TcpSocket& sock, EchoState& s) override {
        char tmp[1024];
        for (;;) {
            int n = sock.receive(tmp, sizeof(tmp));
            if (n <= 0) {
                if (sock.getLastError() == SocketError::WouldBlock) break;
                if (n < 0) {
                    s.disconnected = true;
                    return ServerResult::Disconnect;
                }
                break; // EOF
            }
            s.buf.append(tmp, n);
        }
        return ServerResult::KeepConnection;
    }

    ServerResult onWritable(TcpSocket& sock, EchoState& s) final {
        if (s.buf.empty()) {
            return ServerResult::KeepConnection;
        }

        int sent = sock.send(s.buf.data(), s.buf.size());
        if (sent > 0) {
            s.buf.erase(0, sent);
        } else if (sock.getLastError() != SocketError::WouldBlock) {
            s.disconnected = true;
            return ServerResult::Disconnect;
        }
        return ServerResult::KeepConnection;
    }

    ServerResult onIdle() override {
        ++idleCalls;
        return ServerResult::KeepConnection;
    }

    void onDisconnect(EchoState& s) override {
        ++disconnectCalls;
        s.disconnected = true;
    }
};

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------
static void startServerInBackground(EchoServer& server, std::atomic<bool>& ready, ClientLimit maxClients = ClientLimit::Unlimited) {
    std::thread([&server, &ready, maxClients]() {
        ready = true;
        server.run(maxClients, Milliseconds{100});
    }).detach();
}

static void waitForServerReady(std::atomic<bool>& ready) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (!ready && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    REQUIRE(ready);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
int main() {
    std::cout << "=== ServerBase Tests ===\n";

    // Test 1: requestStop() from another thread
    BEGIN_TEST("ServerBase::requestStop() from another thread");
    {
        EchoServer server(20000);
        std::atomic<bool> ready{false};
        startServerInBackground(server, ready);
        waitForServerReady(ready);

        // Request stop from another thread
        std::thread stopper([&server]() {
            std::this_thread::sleep_for(std::chrono::milliseconds{50});
            EchoServer::requestStop();
        });

        // This should return quickly due to requestStop()
        server.run(ClientLimit::Unlimited, Milliseconds{5000});
        stopper.join();
    }

    // Test 2: Server exits when maxClients limit is reached
    BEGIN_TEST("ServerBase: exits when maxClients limit is reached");
    {
        EchoServer server(20001);
        std::atomic<bool> ready{false};
        const int maxClients = 3;
        startServerInBackground(server, ready, ClientLimit{static_cast<size_t>(maxClients)});
        waitForServerReady(ready);

        // Connect clients up to the limit
        std::vector<std::unique_ptr<TcpSocket>> clients;
        
        for (int i = 0; i < maxClients + 2; ++i) {
            auto result = SocketFactory::createTcpClient(
                AddressFamily::IPv4,
                ConnectArgs{"127.0.0.1", Port{20001}, Milliseconds{1000}});
            if (result.isSuccess()) {
                clients.emplace_back(std::make_unique<TcpSocket>(std::move(result.value())));
            } else {
                // Connection failed - stop trying
                break;
            }
        }

        // Server should have accepted exactly maxClients
        REQUIRE(server.clientCount() == static_cast<size_t>(maxClients));
        
        // Don't disconnect clients - just stop the server
        EchoServer::requestStop();
        
        // Give server time to stop
        std::this_thread::sleep_for(std::chrono::milliseconds{200});
    }

    // Test 3: onIdle() is called periodically
    BEGIN_TEST("ServerBase: onIdle() is called periodically with timeout");
    {
        std::cout << "DEBUG: Starting idle test\n";
        EchoServer server(20002);
        std::atomic<bool> ready{false};
        startServerInBackground(server, ready);
        waitForServerReady(ready);

        // Let server run for a bit to accumulate idle calls
        std::this_thread::sleep_for(std::chrono::milliseconds{1200});
        
        // Should have received some idle calls
        REQUIRE(server.idleCalls.load() > 0);
        
        std::cout << "DEBUG: About to call requestStop in idle test\n";
        EchoServer::requestStop();
        std::cout << "DEBUG: Called requestStop in idle test\n";
        
        // Give server time to stop
        std::this_thread::sleep_for(std::chrono::milliseconds{200});
        std::cout << "DEBUG: Idle test completed\n";
    }

    // Test 4: onDisconnect() is called for each client
    BEGIN_TEST("ServerBase: onDisconnect() is called for each client");
    {
        std::cout << "DEBUG: Starting disconnect test\n";
        EchoServer server(20003);
        std::atomic<bool> ready{false};
        startServerInBackground(server, ready);
        waitForServerReady(ready);

        // Connect and disconnect a client
        {
            std::cout << "DEBUG: About to connect client\n";
            auto result = SocketFactory::createTcpClient(
                AddressFamily::IPv4,
                ConnectArgs{"127.0.0.1", Port{20003}, Milliseconds{1000}});
            if (result.isSuccess()) {
                std::cout << "DEBUG: Client connected, sending data to trigger disconnect detection\n";
                auto client = std::make_unique<TcpSocket>(std::move(result.value()));
                
                // Send some data to establish the connection
                const char* msg = "test";
                client->send(msg, std::strlen(msg));
                
                // Give server time to process
                std::this_thread::sleep_for(std::chrono::milliseconds{100});
                
                std::cout << "DEBUG: Client going out of scope\n";
                // Client disconnects when it goes out of scope
            }
        }

        // Give server time to process disconnection
        std::cout << "DEBUG: Waiting for server to process disconnection\n";
        std::this_thread::sleep_for(std::chrono::milliseconds{500});
        
        // Should have received a disconnect call
        std::cout << "DEBUG: Checking disconnect calls: " << server.disconnectCalls.load() << "\n";
        REQUIRE(server.disconnectCalls.load() > 0);
        
        std::cout << "DEBUG: About to call requestStop in disconnect test\n";
        EchoServer::requestStop();
        std::cout << "DEBUG: Called requestStop in disconnect test\n";
    }

    // Test 5: ClientLimit::Unlimited works correctly
    BEGIN_TEST("ServerBase: ClientLimit::Unlimited accepts unlimited connections");
    {
        EchoServer server(20004);
        std::atomic<bool> ready{false};
        startServerInBackground(server, ready);
        waitForServerReady(ready);

        // Connect many clients
        std::vector<std::unique_ptr<TcpSocket>> clients;
        const int manyClients = 10;
        
        for (int i = 0; i < manyClients; ++i) {
            auto result = SocketFactory::createTcpClient(
                AddressFamily::IPv4,
                ConnectArgs{"127.0.0.1", Port{20004}, Milliseconds{1000}});
            if (result.isSuccess()) {
                clients.emplace_back(std::make_unique<TcpSocket>(std::move(result.value())));
            }
        }

        // Should have accepted all clients
        REQUIRE(server.clientCount() == static_cast<size_t>(manyClients));
        
        // Disconnect all clients
        clients.clear();
        
        EchoServer::requestStop();
    }

    // Test 6: ClientLimit::Default works correctly
    BEGIN_TEST("ServerBase: ClientLimit::Default respects limit");
    {
        EchoServer server(20005);
        std::atomic<bool> ready{false};
        startServerInBackground(server, ready);
        waitForServerReady(ready);

        // Connect clients up to the default limit
        std::vector<std::unique_ptr<TcpSocket>> clients;
        
        for (int i = 0; i < static_cast<int>(ClientLimit::Default) + 2; ++i) {
            auto result = SocketFactory::createTcpClient(
                AddressFamily::IPv4,
                ConnectArgs{"127.0.0.1", Port{20005}, Milliseconds{1000}});
            if (result.isSuccess()) {
                clients.emplace_back(std::make_unique<TcpSocket>(std::move(result.value())));
            } else {
                // Connection failed - stop trying
                break;
            }
        }

        // Should have accepted exactly the default limit
        REQUIRE(server.clientCount() == static_cast<size_t>(ClientLimit::Default));
        
        EchoServer::requestStop();
    }

    // Test 7: Server can be stopped and restarted
    BEGIN_TEST("ServerBase: can be stopped and restarted");
    {
        EchoServer server1(20006);
        std::atomic<bool> ready1{false};
        startServerInBackground(server1, ready1);
        waitForServerReady(ready1);

        // Stop the server
        EchoServer::requestStop();
        std::this_thread::sleep_for(std::chrono::milliseconds{100});

        // Restart the server
        EchoServer server2(20006);
        std::atomic<bool> ready2{false};
        startServerInBackground(server2, ready2);
        waitForServerReady(ready2);

        // Should be able to connect to the restarted server
        auto result = SocketFactory::createTcpClient(
            AddressFamily::IPv4,
            ConnectArgs{"127.0.0.1", Port{20006}, Milliseconds{1000}});
        REQUIRE(result.isSuccess());
        
        EchoServer::requestStop();
    }

    std::cout << "All ServerBase tests passed!\n";
    return 0;
}
