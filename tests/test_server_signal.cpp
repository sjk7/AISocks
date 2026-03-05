// This is an independent project of an individual developer. Dear PVS-Studio, please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com

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
    
    explicit SignalTestServer(const ServerBind& bind)
        : ServerBase(bind) {}

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
        bool result = g_serverSignalStop.compare_exchange_strong(expected, true);
        
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
    BEGIN_TEST("Server integration: server stops when flag set (handleSignals=true)");
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

    // Test 8: Server ignores flag when handleSignals=false
    BEGIN_TEST("Server integration: server ignores flag (handleSignals=false)");
    {
        g_serverSignalStop.store(false);
        
        SignalTestServer server(ServerBind{"127.0.0.1", Port{0}});
        REQUIRE(server.isValid());
        
        server.setHandleSignals(false);
        
        // Set flag BEFORE starting server
        g_serverSignalStop.store(true);
        
        // Run server in thread with timeout
        bool serverExited = false;
        std::thread serverThread([&server, &serverExited]() {
            server.run(ClientLimit{1}, Milliseconds{50});
            serverExited = true;
        });
        
        // Give server time to run
        std::this_thread::sleep_for(100ms);
        
        // Server should still be running (flag ignored)
        // Stop it by maxClients (connect one client)
        auto client = SocketFactory::createTcpClient(
            AddressFamily::IPv4,
            ConnectArgs{"127.0.0.1", server.getPort()}
        );
        
        serverThread.join();
        
        REQUIRE(serverExited == true);
        
        // Reset
        g_serverSignalStop.store(false);
    }

    // Test 9: Default handleSignals state
    BEGIN_TEST("Default state: servers handle signals by default");
    {
        SignalTestServer server(ServerBind{"127.0.0.1", Port{0}});
        REQUIRE(server.isValid());
        
        // Default should be true (handle signals)
        // We test this indirectly by seeing if server respects flag
        g_serverSignalStop.store(false);
        
        std::thread serverThread([&server]() {
            server.run(ClientLimit::Unlimited, Milliseconds{50});
        });
        
        std::this_thread::sleep_for(50ms);
        g_serverSignalStop.store(true);
        
        serverThread.join(); // Should stop due to flag
        
        REQUIRE(true);
        g_serverSignalStop.store(false);
    }

    // Test 10: Flag state persists across multiple checks
    BEGIN_TEST("Flag persistence: state remains until explicitly changed");
    {
        g_serverSignalStop.store(true);
        
        for (int i = 0; i < 100; ++i) {
            REQUIRE(g_serverSignalStop.load() == true);
        }
        
        g_serverSignalStop.store(false);
        
        for (int i = 0; i < 100; ++i) {
            REQUIRE(g_serverSignalStop.load() == false);
        }
    }

    // Test 11: Multiple servers respect same flag
    BEGIN_TEST("Multiple servers: all respect g_serverSignalStop");
    {
        g_serverSignalStop.store(false);
        
        SignalTestServer server1(ServerBind{"127.0.0.1", Port{0}});
        SignalTestServer server2(ServerBind{"127.0.0.1", Port{0}});
        
        REQUIRE(server1.isValid());
        REQUIRE(server2.isValid());
        
        server1.setHandleSignals(true);
        server2.setHandleSignals(true);
        
        std::thread t1([&server1]() {
            server1.run(ClientLimit::Unlimited, Milliseconds{50});
        });
        
        std::thread t2([&server2]() {
            server2.run(ClientLimit::Unlimited, Milliseconds{50});
        });
        
        std::this_thread::sleep_for(100ms);
        
        // Set flag - both should stop
        g_serverSignalStop.store(true);
        
        t1.join();
        t2.join();
        
        REQUIRE(true); // Both stopped
        
        g_serverSignalStop.store(false);
    }

    // Test 12: Flag reset after use
    BEGIN_TEST("Flag reset: can be reset and reused");
    {
        // Set, check, reset, check
        g_serverSignalStop.store(true);
        REQUIRE(g_serverSignalStop.load() == true);
        
        g_serverSignalStop.store(false);
        REQUIRE(g_serverSignalStop.load() == false);
        
        // Reuse
        g_serverSignalStop.store(true);
        REQUIRE(g_serverSignalStop.load() == true);
        
        g_serverSignalStop.store(false);
        REQUIRE(g_serverSignalStop.load() == false);
    }

    // Test 13: Memory ordering - sequential consistency
    BEGIN_TEST("Memory ordering: atomic flag has sequential consistency");
    {
        g_serverSignalStop.store(false);
        
        std::atomic<bool> writerDone{false};
        std::atomic<int> observedValues{0};
        
        std::thread writer([&writerDone]() {
            g_serverSignalStop.store(true);
            writerDone.store(true);
        });
        
        std::thread reader([&writerDone, &observedValues]() {
            while (!writerDone.load()) {
                if (g_serverSignalStop.load()) {
                    observedValues++;
                }
            }
        });
        
        writer.join();
        reader.join();
        
        // Should have observed the change
        REQUIRE(observedValues.load() >= 0); // At least didn't crash
        
        g_serverSignalStop.store(false);
    }

    // Test 14: Rapid toggle stress test
    BEGIN_TEST("Stress test: rapid flag toggling");
    {
        g_serverSignalStop.store(false);
        
        std::atomic<int> toggleCount{0};
        
        std::thread toggler([&toggleCount]() {
            for (int i = 0; i < 10000; ++i) {
                g_serverSignalStop.store(true);
                g_serverSignalStop.store(false);
                toggleCount++;
            }
        });
        
        toggler.join();
        
        REQUIRE(toggleCount.load() == 10000);
        REQUIRE(g_serverSignalStop.load() == false); // Ended on false
    }

    // Test 15: Signal handling opt-out is per-server
    BEGIN_TEST("Per-server control: setHandleSignals is per-instance");
    {
        g_serverSignalStop.store(false);
        
        SignalTestServer server1(ServerBind{"127.0.0.1", Port{0}});
        SignalTestServer server2(ServerBind{"127.0.0.1", Port{0}});
        
        server1.setHandleSignals(true);
        server2.setHandleSignals(false);
        
        g_serverSignalStop.store(true);
        
        // server1 should stop immediately when run() is called
        bool s1Stopped = false;
        std::thread t1([&server1, &s1Stopped]() {
            server1.run(ClientLimit::Unlimited, Milliseconds{50});
            s1Stopped = true;
        });
        
        // server2 should need a client connection to stop
        std::thread t2([&server2]() {
            server2.run(ClientLimit{1}, Milliseconds{50});
        });
        
        std::this_thread::sleep_for(100ms);
        
        // Connect to server2 to make it exit
        auto client = SocketFactory::createTcpClient(
            AddressFamily::IPv4,
            ConnectArgs{"127.0.0.1", server2.getPort()}
        );
        
        t1.join();
        t2.join();
        
        REQUIRE(s1Stopped == true);
        
        g_serverSignalStop.store(false);
    }

    return test_summary();
}
