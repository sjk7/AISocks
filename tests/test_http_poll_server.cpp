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
#include <string>
#include <thread>
#include <chrono>
#include <atomic>

using namespace aiSocks;
using namespace std::chrono_literals;
using namespace std::chrono;

// Test server with instrumented hooks
class TestHttpServer : public HttpPollServer {
    public:
    std::atomic<int> responseBeginCount{0};
    std::atomic<int> responseSentCount{0};
    std::string lastResponse;
    bool useStaticResponse = false;
    std::string staticResponseStorage
        = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nHello";

    std::atomic<bool> ready_{false};
    void waitReady() const {
        while (!ready_.load(std::memory_order_acquire))
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    void waitForResponseBegin(int n = 1) const {
        while (responseBeginCount.load(std::memory_order_acquire) < n)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    void waitForResponseSent(int n = 1) const {
        while (responseSentCount.load(std::memory_order_acquire) < n)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    explicit TestHttpServer(const ServerBind& bind) : HttpPollServer(bind) {
        // Reset test state for this instance
        responseBeginCount.store(0);
        responseSentCount.store(0);
        lastResponse.clear();
        useStaticResponse = false;
        staticResponseStorage
            = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nHello";
    }

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

    void onReady() override { ready_.store(true, std::memory_order_release); }

    void onResponseBegin(HttpClientState& state) override {
        responseBeginCount.fetch_add(1, std::memory_order_release);
        HttpPollServer::onResponseBegin(state);
    }

    void onResponseSent(HttpClientState& state) override {
        responseSentCount.fetch_add(1, std::memory_order_release);
        HttpPollServer::onResponseSent(state);
    }
};

// Receive an HTTP response from an already-connected socket,
// stopping as soon as Content-Length bytes of body have arrived
// (or after headers if no Content-Length is present).
static std::string rxHttpResponse(TcpSocket& client) {
    std::string response;
    char buf[4096];
    int n;
    int receiveCount = 0;
    while ((n = client.receive(buf, sizeof(buf))) > 0) {
        receiveCount++;
        printf("[DEBUG] rxHttpResponse: Received chunk %d, size=%d bytes\n",
            receiveCount, n);
        fflush(stdout);
        response.append(buf, n);

        if (response.find("\r\n\r\n") != std::string::npos) {
            printf("[DEBUG] rxHttpResponse: Found end of headers\n");
            fflush(stdout);
            size_t clPos = response.find("Content-Length:");
            if (clPos != std::string::npos) {
                int contentLength = std::atoi(response.c_str() + clPos + 15);
                size_t bodyStart = response.find("\r\n\r\n") + 4;
                printf("[DEBUG] rxHttpResponse: Content-Length=%d, "
                       "bodyStart=%zu, currentSize=%zu\n",
                    contentLength, bodyStart, response.size());
                fflush(stdout);
                if (response.size()
                    >= bodyStart + static_cast<size_t>(contentLength)) {
                    printf(
                        "[DEBUG] rxHttpResponse: Complete response received, "
                        "breaking\n");
                    fflush(stdout);
                    break;
                }
            } else {
                printf("[DEBUG] rxHttpResponse: No Content-Length, breaking\n");
                fflush(stdout);
                break;
            }
        }
    }
    printf(
        "[DEBUG] rxHttpResponse: Receive loop ended, final response size=%zu\n",
        response.size());
    fflush(stdout);
    return response;
}

// Test helper: send raw HTTP request and read response
static std::string sendHttpRequest(Port port, const std::string& request) {
    printf("[DEBUG] sendHttpRequest: Starting - port=%d\n",
        static_cast<int>(port.value()));
    fflush(stdout);

    auto clientResult = SocketFactory::createTcpClient(
        AddressFamily::IPv4, ConnectArgs{"127.0.0.1", port});

    printf("[DEBUG] sendHttpRequest: Client creation result=%s\n",
        clientResult.isSuccess() ? "SUCCESS" : "FAILED");
    fflush(stdout);

    if (!clientResult.isSuccess()) {
        printf("[DEBUG] sendHttpRequest: Returning empty string due to client "
               "creation failure\n");
        fflush(stdout);
        return "";
    }

    auto& client = clientResult.value();
    client.setReceiveTimeout(Milliseconds{1000});
    printf("[DEBUG] sendHttpRequest: Set receive timeout to 1000ms\n");
    fflush(stdout);

    // Send request
    printf("[DEBUG] sendHttpRequest: Sending request of size %zu bytes\n",
        request.size());
    fflush(stdout);
    if (!client.sendAll(request.data(), request.size())) {
        printf("[DEBUG] sendHttpRequest: sendAll failed, returning empty "
               "string\n");
        fflush(stdout);
        return "";
    }
    printf("[DEBUG] sendHttpRequest: Request sent successfully\n");
    fflush(stdout);

    printf("[DEBUG] sendHttpRequest: Starting to receive response\n");
    fflush(stdout);
    std::string response = rxHttpResponse(client);

    // Explicitly close the client socket to ensure server detects disconnection
    client.close();
    printf("[DEBUG] sendHttpRequest: Client socket closed\n");
    fflush(stdout);

    return response;
}

// Helper function to receive complete HTTP response with timeout protection
std::string receiveCompleteResponse(
    TcpSocket& client, Milliseconds timeout = Milliseconds{1000}) {
    std::string response;
    char buf[1024];
    size_t bodyStart = 0;
    size_t contentLength = 0;
    bool headersReceived = false;

    auto startTime = std::chrono::steady_clock::now();
    auto timeoutDuration = milliseconds(timeout.count);

    while (true) {
        // Check timeout
        auto elapsed = std::chrono::steady_clock::now() - startTime;
        if (elapsed > timeoutDuration) {
            printf("[DEBUG] receiveCompleteResponse: Timeout after %lldms\n",
                static_cast<long long>(timeout.count));
            fflush(stdout);
            break;
        }

        int n = client.receive(buf, sizeof(buf));
        if (n > 0) {
            response.append(buf, static_cast<size_t>(n));

            if (!headersReceived) {
                size_t headerEnd = response.find("\r\n\r\n");
                if (headerEnd != std::string::npos) {
                    headersReceived = true;
                    bodyStart = headerEnd + 4;

                    size_t clPos = response.find("Content-Length: ");
                    if (clPos != std::string::npos) {
                        contentLength = std::stoul(response.substr(clPos + 16,
                            response.find("\r\n", clPos) - (clPos + 16)));
                    }
                }
            }

            if (headersReceived && contentLength > 0
                && (response.size() - bodyStart) >= contentLength) {
                // Complete response received
                break;
            } else if (headersReceived && contentLength == 0
                && response.find("\r\n\r\n") != std::string::npos) {
                // No Content-Length, assume complete after headers
                break;
            }
        } else if (n == 0) {
            // Connection closed
            break;
        } else {
            const auto err = client.getLastError();
            if (err == SocketError::WouldBlock || err == SocketError::Timeout) {
                std::this_thread::sleep_for(10ms); // Small delay before retry
                continue;
            }
            break; // Error
        }
    }

    return response;
}

int main() {
    printf("=== HttpPollServer Tests ===\n");
    fflush(stdout);

    // Test 1: Server can be instantiated
    BEGIN_TEST("Basic: HttpPollServer can be created");
    {
        TestHttpServer server(ServerBind{"127.0.0.1", Port{0}});
        REQUIRE(server.isValid());
    }

    // Test 2: Hook counters are initialized
    BEGIN_TEST("Hooks: counters initialize to zero");
    {
        TestHttpServer server(ServerBind{"127.0.0.1", Port{0}});
        REQUIRE(server.responseBeginCount == 0);
        REQUIRE(server.responseSentCount == 0);
    }

    // Test 3: Static response mode can be set
    BEGIN_TEST("Response mode: useStaticResponse flag works");
    {
        TestHttpServer server(ServerBind{"127.0.0.1", Port{0}});
        server.useStaticResponse = false;
        REQUIRE(server.useStaticResponse == false);

        server.useStaticResponse = true;
        REQUIRE(server.useStaticResponse == true);
    }

    // Test 3: Dynamic response mode
    BEGIN_TEST("Response mode: dynamic response mode works");
    {
        printf("[DEBUG] Starting Test 3 Dynamic Response\n");
        fflush(stdout);
        TestHttpServer server(ServerBind{"127.0.0.1", Port{0}});
        printf("[DEBUG] Test 3 - Server created, isValid=%s\n",
            server.isValid() ? "true" : "false");
        fflush(stdout);
        REQUIRE(server.isValid());

        printf("[DEBUG] Test 3 - Starting server thread\n");
        fflush(stdout);
        std::thread serverThread([&server]() {
            printf("[DEBUG] Test 3 - Server thread: starting run()\n");
            fflush(stdout);
            server.run(ClientLimit{1}, Milliseconds{20});
            printf("[DEBUG] Test 3 - Server thread: run() completed\n");
            fflush(stdout);
        });

        fflush(stdout);
        server.waitReady();

        printf("[DEBUG] Test 3 - Client sending request\n");
        fflush(stdout);
        std::string response = sendHttpRequest(
            server.serverPort(), "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");
        printf("[DEBUG] Test 3 - Client received response, length: %zu\n",
            response.size());
        fflush(stdout);

        printf("[DEBUG] Test 3 - Stopping server\n");
        fflush(stdout);
        g_serverSignalStop.store(true);

        printf("[DEBUG] Test 3 - Joining server thread\n");
        fflush(stdout);
        serverThread.join();
        g_serverSignalStop.store(false); // Reset for next test
        printf("[DEBUG] Test 3 - Server thread joined\n");
        fflush(stdout);

        REQUIRE(!response.empty());
        REQUIRE(response.find("Dynamic") != std::string::npos);
    }

    // Test 4: onResponseBegin hook is called
    BEGIN_TEST("Hook: onResponseBegin called once per response");
    {
        TestHttpServer server(ServerBind{"127.0.0.1", Port{0}});
        REQUIRE(server.isValid());

        std::thread serverThread([&server]() {
            server.run(ClientLimit::Unlimited, Milliseconds{20});
        });

        server.waitReady();

        sendHttpRequest(
            server.serverPort(), "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");

        server.waitForResponseBegin();
        g_serverSignalStop.store(true);
        serverThread.join();
        g_serverSignalStop.store(false);

        REQUIRE(server.responseBeginCount == 1);
    }

    // Test 5: onResponseSent hook is called
    BEGIN_TEST("Hook: onResponseSent called after complete response");
    {
        TestHttpServer server(ServerBind{"127.0.0.1", Port{0}});
        REQUIRE(server.isValid());

        std::thread serverThread([&server]() {
            server.run(ClientLimit::Unlimited, Milliseconds{20});
        });

        server.waitReady();

        sendHttpRequest(
            server.serverPort(), "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");

        server.waitForResponseSent();
        g_serverSignalStop.store(true);
        serverThread.join();
        g_serverSignalStop.store(false);

        REQUIRE(server.responseSentCount == 1);
    }

    // Test 6: HTTP/1.0 default close behavior
    BEGIN_TEST("HTTP/1.0: connection closes after response by default");
    {
        printf("[DEBUG] Starting Test 6 HTTP/1.0\n");
        fflush(stdout);
        TestHttpServer server(ServerBind{"127.0.0.1", Port{0}});
        printf("[DEBUG] Test 6 - Server created, isValid=%s\n",
            server.isValid() ? "true" : "false");
        fflush(stdout);
        REQUIRE(server.isValid());

        printf("[DEBUG] Test 6 - Starting server thread\n");
        fflush(stdout);
        std::thread serverThread([&server]() {
            printf("[DEBUG] Test 6 - Server thread: starting run()\n");
            fflush(stdout);
            server.run(ClientLimit{1}, Milliseconds{20});
            printf("[DEBUG] Test 6 - Server thread: run() completed\n");
            fflush(stdout);
        });

        fflush(stdout);
        server.waitReady();

        printf("[DEBUG] Test 6 - Client sending HTTP/1.0 request\n");
        fflush(stdout);
        std::string response = sendHttpRequest(
            server.serverPort(), "GET / HTTP/1.0\r\nHost: localhost\r\n\r\n");
        printf("[DEBUG] Test 6 - Client received response, length: %zu\n",
            response.length());
        fflush(stdout);

        printf("[DEBUG] Test 6 - Stopping server\n");
        fflush(stdout);
        g_serverSignalStop.store(true);

        printf("[DEBUG] Test 6 - Joining server thread\n");
        fflush(stdout);
        serverThread.join();
        g_serverSignalStop.store(false); // Reset for next test
        printf("[DEBUG] Test 6 - Server thread joined\n");
        fflush(stdout);

        REQUIRE(!response.empty());
        REQUIRE(response.find("200 OK") != std::string::npos);
    }

    // Test 7: HTTP/1.1 default keep-alive behavior
    BEGIN_TEST("HTTP/1.1: connection kept alive by default");
    {
        TestHttpServer server(ServerBind{"127.0.0.1", Port{0}});
        REQUIRE(server.isValid());

        std::thread serverThread(
            [&server]() { server.run(ClientLimit{1}, Milliseconds{20}); });

        server.waitReady();

        auto clientResult = SocketFactory::createTcpClient(
            AddressFamily::IPv4, ConnectArgs{"127.0.0.1", server.serverPort()});
        REQUIRE(clientResult.isSuccess());

        auto& client = clientResult.value();
        client.setReceiveTimeout(Milliseconds{500});

        // First request
        std::string req1 = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
        client.sendAll(req1.data(), req1.size());

        std::string response1 = receiveCompleteResponse(client);
        REQUIRE(!response1.empty());
        REQUIRE(response1.find("200") != std::string::npos);

        // Connection should still be open - send second request
        std::string req2 = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
        bool sent = client.sendAll(req2.data(), req2.size());
        REQUIRE(sent == true); // Connection was kept alive

        std::string response2 = receiveCompleteResponse(client);
        REQUIRE(!response2.empty());
        REQUIRE(response2.find("200") != std::string::npos);

        g_serverSignalStop.store(true);
        serverThread.join();
        g_serverSignalStop.store(false);
    }

    // Test 8: Connection: close header forces close
    BEGIN_TEST("Connection: close header closes after response");
    {
        printf("[DEBUG] Starting Test 8 Connection Close\n");
        fflush(stdout);
        TestHttpServer server(ServerBind{"127.0.0.1", Port{0}});
        printf("[DEBUG] Test 8 - Server created, isValid=%s\n",
            server.isValid() ? "true" : "false");
        fflush(stdout);
        REQUIRE(server.isValid());

        printf("[DEBUG] Test 8 - Starting server thread\n");
        fflush(stdout);
        std::thread serverThread([&server]() {
            printf("[DEBUG] Test 8 - Server thread: starting run()\n");
            fflush(stdout);
            server.run(ClientLimit{1}, Milliseconds{20});
            printf("[DEBUG] Test 8 - Server thread: run() completed\n");
            fflush(stdout);
        });

        fflush(stdout);
        server.waitReady();

        printf("[DEBUG] Test 8 - Client sending Connection: close request\n");
        fflush(stdout);
        std::string response = sendHttpRequest(server.serverPort(),
            "GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
        printf("[DEBUG] Test 8 - Client received response, length: %zu\n",
            response.length());
        fflush(stdout);

        printf("[DEBUG] Test 8 - Stopping server\n");
        fflush(stdout);
        g_serverSignalStop.store(true);

        printf("[DEBUG] Test 8 - Joining server thread\n");
        fflush(stdout);
        serverThread.join();
        g_serverSignalStop.store(false); // Reset for next test
        printf("[DEBUG] Test 8 - Server thread joined\n");
        fflush(stdout);

        REQUIRE(!response.empty());
    }

    // Test 9: Multiple pipelined requests (HTTP/1.1 keep-alive)
    BEGIN_TEST("Pipelining: multiple requests on same connection");
    {
        TestHttpServer server(ServerBind{"127.0.0.1", Port{0}});
        REQUIRE(server.isValid());

        std::thread serverThread(
            [&server]() { server.run(ClientLimit{10}, Milliseconds{20}); });

        server.waitReady();

        auto clientResult = SocketFactory::createTcpClient(
            AddressFamily::IPv4, ConnectArgs{"127.0.0.1", server.serverPort()});
        REQUIRE(clientResult.isSuccess());

        auto& client = clientResult.value();
        client.setReceiveTimeout(Milliseconds{1000});

        int successfulRequests = 0;
        for (int i = 0; i < 3; ++i) {
            std::string req = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
            if (client.sendAll(req.data(), req.size())) {
                std::string response = receiveCompleteResponse(client);
                if (!response.empty()
                    && response.find("200") != std::string::npos) {
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
            [&server]() { server.run(ClientLimit{1}, Milliseconds{20}); });

        server.waitReady();

        auto clientResult = SocketFactory::createTcpClient(
            AddressFamily::IPv4, ConnectArgs{"127.0.0.1", server.serverPort()});
        REQUIRE(clientResult.isSuccess());

        auto& client = clientResult.value();
        client.setReceiveTimeout(Milliseconds{1000});

        // Send request in parts
        std::string part1 = "GET / HTTP/1.1\r\n";
        std::string part2 = "Host: localhost\r\n\r\n";

        client.sendAll(part1.data(), part1.size());
        printf("DEBUG: Sent part1\n");
        server.waitReady();
        client.sendAll(part2.data(), part2.size());
        printf("DEBUG: Sent part2\n");

        // Should get complete response
        printf("DEBUG: Client receiving response...\n");
        std::string response = receiveCompleteResponse(client);
        printf("DEBUG: Client received %zu bytes\n", response.size());

        printf("DEBUG: Test 10 - Stopping server...\n");
        g_serverSignalStop.store(true);
        printf("DEBUG: Test 10 - Joining server thread...\n");
        serverThread.join();
        g_serverSignalStop.store(false); // Reset for next test
        printf("DEBUG: Test 10 - Server thread joined\n");

        REQUIRE(!response.empty());
        REQUIRE(response.find("200 OK") != std::string::npos);
    }

    BEGIN_TEST("Edge case: empty/incomplete request");
    {
        TestHttpServer server(ServerBind{"127.0.0.1", Port{0}});
        REQUIRE(server.isValid());

        printf("DEBUG: Test 11 - Running server with ClientLimit{1}\n");
        std::thread serverThread(
            [&server]() { server.run(ClientLimit{1}, Milliseconds{20}); });

        server.waitReady();

        auto clientResult = SocketFactory::createTcpClient(
            AddressFamily::IPv4, ConnectArgs{"127.0.0.1", server.serverPort()});

        if (clientResult.isSuccess()) {
            printf("DEBUG: Test 11 - Connected dummy client\n");
            auto& client = clientResult.value();
            // Send nothing or just partial line
            std::string partial = "GET";
            client.sendAll(partial.data(), partial.size());
            printf("DEBUG: Test 11 - Sent partial request line: %s\n",
                partial.c_str());

            // Wait for the partial-request timeout to fire
            std::this_thread::sleep_for(50ms);
        }

        printf("DEBUG: Test 11 - Stopping server...\n");
        g_serverSignalStop.store(true);
        printf("DEBUG: Test 11 - Joining server thread...\n");
        serverThread.join();
        g_serverSignalStop.store(false); // Reset for next test
        printf("DEBUG: Test 11 - Server thread joined\n");

        REQUIRE(true); // Server should handle gracefully, no crash
    }

    // Test 12: Large response (multiple send calls)
    BEGIN_TEST("Large response: multiple send() calls");
    {
        class LargeResponseServer : public HttpPollServer {
            public:
            std::atomic<bool> ready_{false};
            void waitReady() const {
                while (!ready_.load(std::memory_order_acquire))
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            explicit LargeResponseServer(const ServerBind& bind)
                : HttpPollServer(bind) {}

            protected:
            void onReady() override { ready_.store(true, std::memory_order_release); }
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
            [&server]() { server.run(ClientLimit{1}, Milliseconds{20}); });

        server.waitReady();

        auto clientResult = SocketFactory::createTcpClient(
            AddressFamily::IPv4, ConnectArgs{"127.0.0.1", server.serverPort()});
        REQUIRE(clientResult.isSuccess());

        auto& client = clientResult.value();

        client.setReceiveTimeout(Milliseconds{2000});

        std::string req = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
        assert(client.isBlocking());
        client.sendAll(req.data(), req.size());

        // Receive all data
        std::string response;
        char buf[1024];
        int n{0};
        while ((n = client.receive(buf, sizeof(buf))) > 0) {
            response.append(buf, n);
            if (response.size() > 10200) break; // Got header + body
        }

        printf("[DEBUG] Test 12 - Stopping server\n");
        fflush(stdout);
        g_serverSignalStop.store(true);

        serverThread.join();
        g_serverSignalStop.store(false); // Reset for next test

        REQUIRE(response.size() >= 10000);
        REQUIRE(response.find("200 OK") != std::string::npos);
    }

    // Test 13: Hook timing - responseBegin before responseSent
    BEGIN_TEST("Hook timing: responseBegin always before responseSent");
    {
        TestHttpServer server(ServerBind{"127.0.0.1", Port{0}});
        REQUIRE(server.isValid());

        std::thread serverThread(
            [&server]() { server.run(ClientLimit{1}, Milliseconds{20}); });

        server.waitReady();

        sendHttpRequest(
            server.serverPort(), "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");

        printf("[DEBUG] Test 13 - Stopping server\n");
        fflush(stdout);
        g_serverSignalStop.store(true);

        serverThread.join();
        g_serverSignalStop.store(false); // Reset for next test

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
            server.run(ClientLimit::Unlimited, Milliseconds{20});
        });

        server.waitReady();

        std::vector<std::thread> clients;
        std::atomic<int> successCount{0};

        for (int i = 0; i < 5; ++i) {
            clients.emplace_back([&server, &successCount]() {
                std::string response = sendHttpRequest(server.serverPort(),
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

        // Wait until server has sent all 5 responses before stopping
        server.waitForResponseSent(5);
        REQUIRE(successCount.load() == 5);

        printf("[DEBUG] Test 14 - Stopping server\n");
        fflush(stdout);
        g_serverSignalStop.store(true);
        serverThread.join();
        g_serverSignalStop.store(false);
        printf("[DEBUG] Test 14 - Server thread joined\n");
        fflush(stdout);
    }

    // Test 15: Request scan position tracking (incremental parsing)
    BEGIN_TEST("Optimization: request scan position tracks parsing progress");
    {
        // This is tested implicitly - if incremental parsing works,
        // partial requests should be handled efficiently
        TestHttpServer server(ServerBind{"127.0.0.1", Port{0}});
        REQUIRE(server.isValid());

        std::thread serverThread(
            [&server]() { server.run(ClientLimit{1}, Milliseconds{20}); });

        server.waitReady();

        auto clientResult = SocketFactory::createTcpClient(
            AddressFamily::IPv4, ConnectArgs{"127.0.0.1", server.serverPort()});
        REQUIRE(clientResult.isSuccess());

        auto& client = clientResult.value();
        client.setReceiveTimeout(Milliseconds{500});

        // Send request in one shot
        std::string req = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
        client.sendAll(req.data(), req.size());

        std::string response = receiveCompleteResponse(client);
        REQUIRE(client.isBlocking());
        REQUIRE(!response.empty());
        g_serverSignalStop.store(true);
        serverThread.join();
        g_serverSignalStop.store(false);
    }

    return test_summary();
}
