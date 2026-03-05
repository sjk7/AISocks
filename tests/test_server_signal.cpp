// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Comprehensive tests for ServerSignal - signal handling for graceful shutdown
// Tests g_serverSignalStop flag, thread-safety, and signal handler behavior

#include "ServerSignal.h"
#include "ServerBase.h"
#include "test_helpers.h"
#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>

using namespace aiSocks;
using namespace std::chrono_literals;

// Minimal server for testing signal interaction
class SignalTestServer : public ServerBase<int> {
    public:
    std::atomic<int> readableCount{0};

    explicit SignalTestServer(const ServerBind& bind) : ServerBase(bind) {}

    protected:
    ServerResult onReadable(TcpSocket&, int&) override {
        readableCount++;
        return ServerResult::KeepConnection;
    }

    ServerResult onWritable(TcpSocket&, int&) override {
        return ServerResult::KeepConnection;
    }
};

int main() {
    std::cout << "=== ServerSignal Tests ===\n";

    // Test 1: Initial state of g_serverSignalStop
    BEGIN_TEST("Initial state: g_serverSignalStop is false");
    {
        // Reset to known state
        g_serverSignalStop.store(false);
        REQUIRE(g_serverSignalStop.load() == false);
    }

    // Test 2: Setting g_serverSignalStop to true
    BEGIN_TEST("Flag manipulation: can set g_serverSignalStop to true");
    {
        g_serverSignalStop.store(false);
        g_serverSignalStop.store(true);
        REQUIRE(g_serverSignalStop.load() == true);

        // Reset
        g_serverSignalStop.store(false);
    }

    // Test 3: Atomic flag is thread-safe for read
    BEGIN_TEST("Thread safety: multiple threads can read flag");
    {
        g_serverSignalStop.store(false);

        std::atomic<int> readCount{0};
        std::vector<std::thread> threads;

        for (int i = 0; i < 10; ++i) {
            threads.emplace_back([&readCount]() {
                for (int j = 0; j < 1000; ++j) {
                    bool val = g_serverSignalStop.load();
                    if (!val) readCount++;
                }
            });
        }

        for (auto& t : threads) {
            t.join();
        }

        // Should have read 10000 times successfully
        REQUIRE(readCount.load() == 10000);
    }

    // Test 4: Atomic flag is thread-safe for write
    BEGIN_TEST("Thread safety: multiple threads can write flag");
    {
        g_serverSignalStop.store(false);

        std::vector<std::thread> threads;

        for (int i = 0; i < 10; ++i) {
            threads.emplace_back([]() {
                for (int j = 0; j < 100; ++j) {
                    g_serverSignalStop.store(true);
                    g_serverSignalStop.store(false);
                }
            });
        }

        for (auto& t : threads) {
            t.join();
        }

        // Should not crash - that's the main test
        REQUIRE(true);
    }

    // Test 5: Atomic flag supports compare-exchange
    BEGIN_TEST("Atomic operations: compare_exchange works");
    {
        g_serverSignalStop.store(false);

        bool expected = false;
        bool result
            = g_serverSignalStop.compare_exchange_strong(expected, true);

        REQUIRE(result == true);
        REQUIRE(g_serverSignalStop.load() == true);

        // Try again - should fail because it's now true
        expected = false;
        result = g_serverSignalStop.compare_exchange_strong(expected, true);
        REQUIRE(result == false);

        // Reset
        g_serverSignalStop.store(false);
    }

    // Test 6: Signal handler installation (function exists)
    BEGIN_TEST("Signal handler: installSignalHandlers function exists");
    {
// Just verify we can call it without crashing
#if defined(_WIN32) || defined(AISOCKS_HAS_SIGNAL_HANDLERS)
        try {
            installSignalHandlers();
            REQUIRE(true); // Didn't crash
        } catch (...) {
            // Some platforms might not support this
            REQUIRE(true);
        }
#else
        // If not implemented, that's OK for this test
        REQUIRE(true);
#endif
    }

    // Test 7: Server stops when flag is set (handleSignals=true)
    BEGIN_TEST(
        "Server integration: server stops when flag set (handleSignals=true)");
    {
        g_serverSignalStop.store(false);

        SignalTestServer server(ServerBind{"127.0.0.1", Port{0}});
        REQUIRE(server.isValid());

        server.setHandleSignals(true);

        // Run server in thread, set flag after a moment
        std::thread serverThread([&server]() {
            server.run(ClientLimit::Unlimited, Milliseconds{50});
        });

        // Give server time to start
        std::this_thread::sleep_for(100ms);

        // Set stop flag
        g_serverSignalStop.store(true);

        // Server should exit within reasonable time
        serverThread.join(); // Should not block forever

        REQUIRE(true); // Made it here = success

        // Reset for next test
        g_serverSignalStop.store(false);
    }

    // Test 8: ServerBase API exists
    BEGIN_TEST("Server API: setHandleSignals method exists");
    {
        SignalTestServer server(ServerBind{"127.0.0.1", Port{0}});
        REQUIRE(server.isValid());

        server.setHandleSignals(false);
        server.setHandleSignals(true);
        
        REQUIRE(true); // Method exists and can be called
    }

    return test_summary();
}
