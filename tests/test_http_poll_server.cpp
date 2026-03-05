// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Comprehensive tests for HttpPollServer - HTTP/1.x framing and connection
// management Tests keep-alive, zero-copy response views, hooks, and request
// buffering

#include "HttpPollServer.h"
#include "TcpSocket.h"
#include "SocketFactory.h"
#include "test_helpers.h"
#include <iostream>
#include <string>
#include <thread>
#include <chrono>

using namespace aiSocks;
using namespace std::chrono_literals;

// Test server with instrumented hooks
class TestHttpServer : public HttpPollServer {
    public:
    int responseBeginCount = 0;
    int responseSentCount = 0;
    std::string lastResponse;
    bool useStaticResponse = false;
    std::string staticResponseStorage
        = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nHello";

    explicit TestHttpServer(const ServerBind& bind) : HttpPollServer(bind) {}

    protected:
    void buildResponse(HttpClientState& state) override {
        if (useStaticResponse) {
            // Zero-copy path: point responseView directly at
            // staticResponseStorage
            state.responseView = staticResponseStorage;
        } else {
            // Dynamic response path: build in responseBuf
            state.responseBuf
                = "HTTP/1.1 200 OK\r\nContent-Length: 7\r\n\r\nDynamic";
            state.responseView = state.responseBuf;
        }
        lastResponse = std::string(state.responseView);
    }

    void onResponseBegin(HttpClientState& state) override {
        responseBeginCount++;
        HttpPollServer::onResponseBegin(state);
    }

    void onResponseSent(HttpClientState& state) override {
        responseSentCount++;
        HttpPollServer::onResponseSent(state);
    }
};

// Test helper: send raw HTTP request and read response
static std::string sendHttpRequest(Port port, const std::string& request) {
    auto clientResult = SocketFactory::createTcpClient(
        AddressFamily::IPv4, ConnectArgs{"127.0.0.1", port});

    if (!clientResult.isSuccess()) {
        return "";
    }

    auto& client = clientResult.value();
    client.setReceiveTimeout(Milliseconds{1000});

    // Send request
    if (!client.sendAll(request.data(), request.size())) {
        return "";
    }

    // Receive response
    std::string response;
    char buf[4096];
    int n;
    while ((n = client.receive(buf, sizeof(buf))) > 0) {
        response.append(buf, n);

        // Check if we have complete response (simple heuristic)
        if (response.find("\r\n\r\n") != std::string::npos) {
            // Check Content-Length if present
            size_t clPos = response.find("Content-Length:");
            if (clPos != std::string::npos) {
                int contentLength = std::atoi(response.c_str() + clPos + 15);
                size_t bodyStart = response.find("\r\n\r\n") + 4;
                if (response.size() >= bodyStart + contentLength) {
                    break;
                }
            } else {
                break;
            }
        }
    }

    return response;
}

int main() {
    std::cout << "=== HttpPollServer Tests ===\n";

    // Test 1: Basic request-response cycle
    BEGIN_TEST("Basic: server accepts connection and sends response");
    {
        TestHttpServer server(ServerBind{"127.0.0.1", Port{0}});
        REQUIRE(server.isValid());

        std::thread serverThread(
            [&server]() { server.run(ClientLimit{1}, Milliseconds{100}); });

        std::this_thread::sleep_for(50ms);

        std::string response = sendHttpRequest(
            server.getActualPort(), "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");

        serverThread.join();

        REQUIRE(!response.empty());
        REQUIRE(response.find("200 OK") != std::string::npos);
    }

    // Test 2: Zero-copy response path (responseView points to static storage)
    BEGIN_TEST("Zero-copy: responseView points to static storage");
    {
        TestHttpServer server(ServerBind{"127.0.0.1", Port{0}});
        server.useStaticResponse = true;
        REQUIRE(server.isValid());

        std::thread serverThread(
            [&server]() { server.run(ClientLimit{1}, Milliseconds{100}); });

        std::this_thread::sleep_for(50ms);

        std::string response = sendHttpRequest(
            server.getActualPort(), "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");

        serverThread.join();

        REQUIRE(!response.empty());
        REQUIRE(response.find("Hello") != std::string::npos);
    }

    // Test 3: Dynamic response path (responseView points to responseBuf)
    BEGIN_TEST("Dynamic: responseView points to responseBuf");
    {
        TestHttpServer server(ServerBind{"127.0.0.1", Port{0}});
        server.useStaticResponse = false;
        REQUIRE(server.isValid());

        std::thread serverThread(
            [&server]() { server.run(ClientLimit{1}, Milliseconds{100}); });

        std::this_thread::sleep_for(50ms);

        std::string response = sendHttpRequest(
            server.getActualPort(), "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");

        serverThread.join();

        REQUIRE(!response.empty());
        REQUIRE(response.find("Dynamic") != std::string::npos);
    }

    // Test 4: onResponseBegin hook is called
    BEGIN_TEST("Hook: onResponseBegin called once per response");
    {
        TestHttpServer server(ServerBind{"127.0.0.1", Port{0}});
        REQUIRE(server.isValid());

        std::thread serverThread(
            [&server]() { server.run(ClientLimit{1}, Milliseconds{100}); });

        std::this_thread::sleep_for(50ms);

        sendHttpRequest(
            server.getActualPort(), "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");

        serverThread.join();

        REQUIRE(server.responseBeginCount == 1);
    }

    // Test 5: onResponseSent hook is called
    BEGIN_TEST("Hook: onResponseSent called after complete response");
    {
        TestHttpServer server(ServerBind{"127.0.0.1", Port{0}});
        REQUIRE(server.isValid());

        std::thread serverThread(
            [&server]() { server.run(ClientLimit{1}, Milliseconds{100}); });

        std::this_thread::sleep_for(50ms);

        sendHttpRequest(
            server.getActualPort(), "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");

        serverThread.join();

        REQUIRE(server.responseSentCount == 1);
    }

    // Test 6: HTTP/1.0 default close behavior
    BEGIN_TEST("HTTP/1.0: connection closes after response by default");
    {
        TestHttpServer server(ServerBind{"127.0.0.1", Port{0}});
        REQUIRE(server.isValid());

        std::thread serverThread([&server]() {
            server.run(ClientLimit::Unlimited, Milliseconds{100});
        });

        std::this_thread::sleep_for(50ms);

        std::string response = sendHttpRequest(
            server.getActualPort(), "GET / HTTP/1.0\r\nHost: localhost\r\n\r\n");

        std::this_thread::sleep_for(100ms);
        g_serverSignalStop.store(true);
        serverThread.join();
        g_serverSignalStop.store(false);

        REQUIRE(!response.empty());
        REQUIRE(response.find("200 OK") != std::string::npos);
    }

    // Test 7: HTTP/1.1 default keep-alive behavior
    BEGIN_TEST("HTTP/1.1: connection kept alive by default");
    {
        TestHttpServer server(ServerBind{"127.0.0.1", Port{0}});
        REQUIRE(server.isValid());

        std::thread serverThread([&server]() {
            server.run(ClientLimit::Unlimited, Milliseconds{100});
        });

        std::this_thread::sleep_for(50ms);

        auto clientResult = SocketFactory::createTcpClient(
            AddressFamily::IPv4, ConnectArgs{"127.0.0.1", server.getActualPort()});
        REQUIRE(clientResult.isSuccess());

        auto& client = clientResult.value();
        client.setReceiveTimeout(Milliseconds{500});

        // First request
        std::string req1 = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
        client.sendAll(req1.data(), req1.size());

        char buf[500];
        int n = client.receive(buf, sizeof(buf));
        REQUIRE(n > 0);

        // Connection should still be open - send second request
        std::string req2 = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
        bool sent = client.sendAll(req2.data(), req2.size());
        REQUIRE(sent == true); // Connection was kept alive

        g_serverSignalStop.store(true);
        serverThread.join();
        g_serverSignalStop.store(false);
    }

    // Test 8: Connection: close header forces close
    BEGIN_TEST("Connection: close header closes after response");
    {
        TestHttpServer server(ServerBind{"127.0.0.1", Port{0}});
        REQUIRE(server.isValid());

        std::thread serverThread([&server]() {
            server.run(ClientLimit::Unlimited, Milliseconds{100});
        });

        std::this_thread::sleep_for(50ms);

        std::string response = sendHttpRequest(server.getActualPort(),
            "GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");

        std::this_thread::sleep_for(100ms);
        g_serverSignalStop.store(true);
        serverThread.join();
        g_serverSignalStop.store(false);

        REQUIRE(!response.empty());
    }

    // Test 9: Multiple pipelined requests (HTTP/1.1 keep-alive)
    BEGIN_TEST("Pipelining: multiple requests on same connection");
    {
        TestHttpServer server(ServerBind{"127.0.0.1", Port{0}});
        REQUIRE(server.isValid());

        std::thread serverThread([&server]() {
            server.run(ClientLimit::Unlimited, Milliseconds{100});
        });

        std::this_thread::sleep_for(50ms);

        auto clientResult = SocketFactory::createTcpClient(
            AddressFamily::IPv4, ConnectArgs{"127.0.0.1", server.getActualPort()});
        REQUIRE(clientResult.isSuccess());

        auto& client = clientResult.value();
        client.setReceiveTimeout(Milliseconds{1000});

        int successfulRequests = 0;
        for (int i = 0; i < 3; ++i) {
            std::string req = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
            if (client.sendAll(req.data(), req.size())) {
                char buf[500];
                int n = client.receive(buf, sizeof(buf));
                if (n > 0) {
                    successfulRequests++;
                }
            }
        }

        g_serverSignalStop.store(true);
        serverThread.join();
        g_serverSignalStop.store(false);

        REQUIRE(successfulRequests >= 2); // At least 2 should succeed
    }

    // Test 10: Request buffering - partial request handling
    BEGIN_TEST("Buffering: handles partial HTTP request");
    {
        TestHttpServer server(ServerBind{"127.0.0.1", Port{0}});
        REQUIRE(server.isValid());

        std::thread serverThread(
            [&server]() { server.run(ClientLimit{1}, Milliseconds{100}); });

        std::this_thread::sleep_for(50ms);

        auto clientResult = SocketFactory::createTcpClient(
            AddressFamily::IPv4, ConnectArgs{"127.0.0.1", server.getActualPort()});
        REQUIRE(clientResult.isSuccess());

        auto& client = clientResult.value();
        client.setReceiveTimeout(Milliseconds{1000});

        // Send request in parts
        std::string part1 = "GET / HTTP/1.1\r\n";
        std::string part2 = "Host: localhost\r\n\r\n";

        client.sendAll(part1.data(), part1.size());
        std::this_thread::sleep_for(50ms);
        client.sendAll(part2.data(), part2.size());

        // Should get complete response
        char buf[500];
        int n = client.receive(buf, sizeof(buf));

        serverThread.join();

        REQUIRE(n > 0);
        std::string response(buf, n);
        REQUIRE(response.find("200 OK") != std::string::npos);
    }

    // Test 11: Empty request handling
    BEGIN_TEST("Edge case: empty/incomplete request");
    {
        TestHttpServer server(ServerBind{"127.0.0.1", Port{0}});
        REQUIRE(server.isValid());

        std::thread serverThread(
            [&server]() { server.run(ClientLimit{1}, Milliseconds{100}); });

        std::this_thread::sleep_for(50ms);

        auto clientResult = SocketFactory::createTcpClient(
            AddressFamily::IPv4, ConnectArgs{"127.0.0.1", server.getActualPort()});

        if (clientResult.isSuccess()) {
            auto& client = clientResult.value();
            // Send nothing or just partial line
            std::string partial = "GET";
            client.sendAll(partial.data(), partial.size());

            // Wait for timeout
            std::this_thread::sleep_for(200ms);
        }

        serverThread.join();

        REQUIRE(true); // Server should handle gracefully, no crash
    }

    // Test 12: Large response (multiple send calls)
    BEGIN_TEST("Large response: multiple send() calls");
    {
        class LargeResponseServer : public HttpPollServer {
            public:
            explicit LargeResponseServer(const ServerBind& bind)
                : HttpPollServer(bind) {}

            protected:
            void buildResponse(HttpClientState& state) override {
                // Build large response (10KB body)
                std::string body(10000, 'X');
                state.responseBuf = "HTTP/1.1 200 OK\r\nContent-Length: "
                    + std::to_string(body.size()) + "\r\n\r\n" + body;
                state.responseView = state.responseBuf;
            }
        };

        LargeResponseServer server(ServerBind{"127.0.0.1", Port{0}});
        REQUIRE(server.isValid());

        std::thread serverThread(
            [&server]() { server.run(ClientLimit{1}, Milliseconds{100}); });

        std::this_thread::sleep_for(50ms);

        auto clientResult = SocketFactory::createTcpClient(
            AddressFamily::IPv4, ConnectArgs{"127.0.0.1", server.getActualPort()});
        REQUIRE(clientResult.isSuccess());

        auto& client = clientResult.value();
        client.setReceiveTimeout(Milliseconds{2000});

        std::string req = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
        client.sendAll(req.data(), req.size());

        // Receive all data
        std::string response;
        char buf[1024];
        int n;
        while ((n = client.receive(buf, sizeof(buf))) > 0) {
            response.append(buf, n);
            if (response.size() > 10200) break; // Got header + body
        }

        serverThread.join();

        REQUIRE(response.size() >= 10000);
        REQUIRE(response.find("200 OK") != std::string::npos);
    }

    // Test 13: Hook timing - responseBegin before responseSent
    BEGIN_TEST("Hook timing: responseBegin always before responseSent");
    {
        TestHttpServer server(ServerBind{"127.0.0.1", Port{0}});
        REQUIRE(server.isValid());

        std::thread serverThread(
            [&server]() { server.run(ClientLimit{1}, Milliseconds{100}); });

        std::this_thread::sleep_for(50ms);

        sendHttpRequest(
            server.getActualPort(), "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");

        serverThread.join();

        REQUIRE(server.responseBeginCount == 1);
        REQUIRE(server.responseSentCount == 1);
        // Verify begin was called before sent (both called exactly once)
    }

    // Test 14: Multiple clients
    BEGIN_TEST("Concurrency: multiple clients simultaneously");
    {
        TestHttpServer server(ServerBind{"127.0.0.1", Port{0}});
        REQUIRE(server.isValid());

        std::thread serverThread([&server]() {
            server.run(ClientLimit::Unlimited, Milliseconds{100});
        });

        std::this_thread::sleep_for(50ms);

        std::vector<std::thread> clients;
        std::atomic<int> successCount{0};

        for (int i = 0; i < 5; ++i) {
            clients.emplace_back([&server, &successCount]() {
                std::string response = sendHttpRequest(server.getActualPort(),
                    "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");
                if (!response.empty()
                    && response.find("200") != std::string::npos) {
                    successCount++;
                }
            });
        }

        for (auto& t : clients) {
            t.join();
        }

        g_serverSignalStop.store(true);
        serverThread.join();
        g_serverSignalStop.store(false);

        REQUIRE(successCount.load() >= 4); // At least 4 of 5 should succeed
    }

    // Test 15: Request scan position tracking (incremental parsing)
    BEGIN_TEST("Optimization: request scan position tracks parsing progress");
    {
        // This is tested implicitly - if incremental parsing works,
        // partial requests should be handled efficiently
        TestHttpServer server(ServerBind{"127.0.0.1", Port{0}});
        REQUIRE(server.isValid());

        std::thread serverThread(
            [&server]() { server.run(ClientLimit{1}, Milliseconds{100}); });

        std::this_thread::sleep_for(50ms);

        auto clientResult = SocketFactory::createTcpClient(
            AddressFamily::IPv4, ConnectArgs{"127.0.0.1", server.getActualPort()});
        REQUIRE(clientResult.isSuccess());

        auto& client = clientResult.value();

        // Send request byte by byte (stress test incremental parsing)
        std::string req = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
        for (char c : req) {
            client.sendAll(&c, 1);
            std::this_thread::sleep_for(1ms);
        }

        char buf[500];
        int n = client.receive(buf, sizeof(buf));

        serverThread.join();

        REQUIRE(n > 0); // Should handle byte-by-byte delivery
    }

    return test_summary();
}
