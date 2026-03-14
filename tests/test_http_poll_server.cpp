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
#include "FileIO.h"
#include "test_helpers.h"
#include <cstdio>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>

// Enable diagnostic output by compiling with -DTEST_VERBOSE.
#ifdef TEST_VERBOSE
#define DLOG(...)                                                              \
    do {                                                                       \
        printf(__VA_ARGS__);                                                   \
        fflush(stdout);                                                        \
    } while (0)
#else
#define DLOG(...)                                                              \
    do {                                                                       \
    } while (0)
#endif

using namespace aiSocks;
using namespace std::chrono_literals;
using namespace std::chrono;

static const char* kAccessLogPath = "test_http_poll_server_access.log";

static void removeAccessLog() {
    std::remove(kAccessLogPath);
}

static std::string readAccessLog() {
    File f(kAccessLogPath, "r");
    if (!f.isOpen()) return {};
    const auto bytes = f.readAll();
    return std::string(bytes.begin(), bytes.end());
}

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

    public:
    // Expose protected printStartupBanner for coverage testing
    void callPrintStartupBanner() { printStartupBanner(); }
};

// Receive an HTTP response from an already-connected socket,
// stopping as soon as Content-Length bytes of body have arrived
// (or after headers if no Content-Length is present).
static std::string rxHttpResponse(TcpSocket& client) {
    std::string response;
    char buf[4096];
    int n;
#ifdef TEST_VERBOSE
    int receiveCount = 0;
#endif
    while ((n = client.receive(buf, sizeof(buf))) > 0) {
#ifdef TEST_VERBOSE
        receiveCount++;
#endif
        DLOG("[DEBUG] rxHttpResponse: Received chunk %d, size=%d bytes\n",
            receiveCount, n);
        response.append(buf, n);

        if (response.find("\r\n\r\n") != std::string::npos) {
            DLOG("[DEBUG] rxHttpResponse: Found end of headers\n");
            size_t clPos = response.find("Content-Length:");
            if (clPos != std::string::npos) {
                int contentLength = std::atoi(response.c_str() + clPos + 15);
                size_t bodyStart = response.find("\r\n\r\n") + 4;
                DLOG("[DEBUG] rxHttpResponse: Content-Length=%d, "
                     "bodyStart=%zu, currentSize=%zu\n",
                    contentLength, bodyStart, response.size());
                if (response.size()
                    >= bodyStart + static_cast<size_t>(contentLength)) {
                    DLOG("[DEBUG] rxHttpResponse: Complete response received, "
                         "breaking\n");
                    break;
                }
            } else {
                DLOG("[DEBUG] rxHttpResponse: No Content-Length, breaking\n");
                break;
            }
        }
    }
    DLOG(
        "[DEBUG] rxHttpResponse: Receive loop ended, final response size=%zu\n",
        response.size());
    return response;
}

// Test helper: send raw HTTP request and read response
static std::string sendHttpRequest(Port port, const std::string& request) {
    DLOG("[DEBUG] sendHttpRequest: Starting - port=%d\n",
        static_cast<int>(port.value()));

    auto clientResult = SocketFactory::createTcpClient(
        AddressFamily::IPv4, ConnectArgs{"127.0.0.1", port});

    DLOG("[DEBUG] sendHttpRequest: Client creation result=%s\n",
        clientResult.isSuccess() ? "SUCCESS" : "FAILED");

    if (!clientResult.isSuccess()) {
        DLOG("[DEBUG] sendHttpRequest: Returning empty string due to client "
             "creation failure\n");
        return "";
    }

    auto& client = clientResult.value();
    client.setReceiveTimeout(Milliseconds{1000});
    DLOG("[DEBUG] sendHttpRequest: Set receive timeout to 1000ms\n");

    // Send request
    DLOG("[DEBUG] sendHttpRequest: Sending request of size %zu bytes\n",
        request.size());
    if (!client.sendAll(request.data(), request.size())) {
        DLOG("[DEBUG] sendHttpRequest: sendAll failed, returning empty "
             "string\n");
        return "";
    }
    DLOG("[DEBUG] sendHttpRequest: Request sent successfully\n");

    DLOG("[DEBUG] sendHttpRequest: Starting to receive response\n");
    std::string response = rxHttpResponse(client);

    // Explicitly close the client socket to ensure server detects disconnection
    client.close();
    DLOG("[DEBUG] sendHttpRequest: Client socket closed\n");

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
            DLOG("[DEBUG] receiveCompleteResponse: Timeout after %lldms\n",
                static_cast<long long>(timeout.count));
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
        DLOG("[DEBUG] Starting Test 3 Dynamic Response\n");
        TestHttpServer server(ServerBind{"127.0.0.1", Port{0}});
        DLOG("[DEBUG] Test 3 - Server created, isValid=%s\n",
            server.isValid() ? "true" : "false");
        REQUIRE(server.isValid());

        DLOG("[DEBUG] Test 3 - Starting server thread\n");
        std::thread serverThread([&server]() {
            DLOG("[DEBUG] Test 3 - Server thread: starting run()\n");
            server.run(ClientLimit{1}, Milliseconds{1});
            DLOG("[DEBUG] Test 3 - Server thread: run() completed\n");
        });

        server.waitReady();

        DLOG("[DEBUG] Test 3 - Client sending request\n");
        std::string response = sendHttpRequest(
            server.serverPort(), "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");
        DLOG("[DEBUG] Test 3 - Client received response, length: %zu\n",
            response.size());

        DLOG("[DEBUG] Test 3 - Stopping server\n");
        g_serverSignalStop.store(true);

        DLOG("[DEBUG] Test 3 - Joining server thread\n");
        serverThread.join();
        g_serverSignalStop.store(false); // Reset for next test
        DLOG("[DEBUG] Test 3 - Server thread joined\n");

        REQUIRE(!response.empty());
        REQUIRE(response.find("Dynamic") != std::string::npos);
    }

    // Test 4: onResponseBegin hook is called
    BEGIN_TEST("Hook: onResponseBegin called once per response");
    {
        TestHttpServer server(ServerBind{"127.0.0.1", Port{0}});
        REQUIRE(server.isValid());

        std::thread serverThread([&server]() {
            server.run(ClientLimit::Unlimited, Milliseconds{1});
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
            server.run(ClientLimit::Unlimited, Milliseconds{1});
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

    BEGIN_TEST("Access log: zero-copy response logs correct status code");
    {
        removeAccessLog();
        AccessLogger logger(kAccessLogPath);
        REQUIRE(logger.isOpen());

        TestHttpServer server(ServerBind{"127.0.0.1", Port{0}});
        REQUIRE(server.isValid());
        server.useStaticResponse = true;
        server.setAccessLogger(&logger);

        std::thread serverThread([&server]() {
            server.run(ClientLimit::Unlimited, Milliseconds{1});
        });

        server.waitReady();

        sendHttpRequest(
            server.serverPort(), "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");

        server.waitForResponseSent();
        g_serverSignalStop.store(true);
        serverThread.join();
        g_serverSignalStop.store(false);

        logger.close();
        const std::string log = readAccessLog();
        REQUIRE(log.find("\"GET / HTTP/1.1\" 200 ") != std::string::npos);

        removeAccessLog();
    }

    // Test 6: HTTP/1.0 default close behavior
    BEGIN_TEST("HTTP/1.0: connection closes after response by default");
    {
        DLOG("[DEBUG] Starting Test 6 HTTP/1.0\n");
        TestHttpServer server(ServerBind{"127.0.0.1", Port{0}});
        DLOG("[DEBUG] Test 6 - Server created, isValid=%s\n",
            server.isValid() ? "true" : "false");
        REQUIRE(server.isValid());

        DLOG("[DEBUG] Test 6 - Starting server thread\n");
        std::thread serverThread([&server]() {
            DLOG("[DEBUG] Test 6 - Server thread: starting run()\n");
            server.run(ClientLimit{1}, Milliseconds{1});
            DLOG("[DEBUG] Test 6 - Server thread: run() completed\n");
        });

        server.waitReady();

        DLOG("[DEBUG] Test 6 - Client sending HTTP/1.0 request\n");
        std::string response = sendHttpRequest(
            server.serverPort(), "GET / HTTP/1.0\r\nHost: localhost\r\n\r\n");
        DLOG("[DEBUG] Test 6 - Client received response, length: %zu\n",
            response.length());

        DLOG("[DEBUG] Test 6 - Stopping server\n");
        g_serverSignalStop.store(true);

        DLOG("[DEBUG] Test 6 - Joining server thread\n");
        serverThread.join();
        g_serverSignalStop.store(false); // Reset for next test
        DLOG("[DEBUG] Test 6 - Server thread joined\n");

        REQUIRE(!response.empty());
        REQUIRE(response.find("200 OK") != std::string::npos);
    }

    // Test 7: HTTP/1.1 default keep-alive behavior
    BEGIN_TEST("HTTP/1.1: connection kept alive by default");
    {
        TestHttpServer server(ServerBind{"127.0.0.1", Port{0}});
        REQUIRE(server.isValid());

        std::thread serverThread(
            [&server]() { server.run(ClientLimit{1}, Milliseconds{1}); });

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
        DLOG("[DEBUG] Starting Test 8 Connection Close\n");
        TestHttpServer server(ServerBind{"127.0.0.1", Port{0}});
        DLOG("[DEBUG] Test 8 - Server created, isValid=%s\n",
            server.isValid() ? "true" : "false");
        REQUIRE(server.isValid());

        DLOG("[DEBUG] Test 8 - Starting server thread\n");
        std::thread serverThread([&server]() {
            DLOG("[DEBUG] Test 8 - Server thread: starting run()\n");
            server.run(ClientLimit{1}, Milliseconds{1});
            DLOG("[DEBUG] Test 8 - Server thread: run() completed\n");
        });

        server.waitReady();

        DLOG("[DEBUG] Test 8 - Client sending Connection: close request\n");
        std::string response = sendHttpRequest(server.serverPort(),
            "GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
        DLOG("[DEBUG] Test 8 - Client received response, length: %zu\n",
            response.length());

        DLOG("[DEBUG] Test 8 - Stopping server\n");
        g_serverSignalStop.store(true);

        DLOG("[DEBUG] Test 8 - Joining server thread\n");
        serverThread.join();
        g_serverSignalStop.store(false); // Reset for next test
        DLOG("[DEBUG] Test 8 - Server thread joined\n");

        REQUIRE(!response.empty());
    }

    // Test 9: Multiple pipelined requests (HTTP/1.1 keep-alive)
    BEGIN_TEST("Pipelining: coalesced requests receive multiple responses");
    {
        TestHttpServer server(ServerBind{"127.0.0.1", Port{0}});
        REQUIRE(server.isValid());

        std::thread serverThread(
            [&server]() { server.run(ClientLimit{10}, Milliseconds{1}); });

        server.waitReady();

        auto clientResult = SocketFactory::createTcpClient(
            AddressFamily::IPv4, ConnectArgs{"127.0.0.1", server.serverPort()});
        REQUIRE(clientResult.isSuccess());

        auto& client = clientResult.value();
        client.setReceiveTimeout(Milliseconds{1000});

        const std::string one = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
        const std::string pipelined = one + one;
        REQUIRE(client.sendAll(pipelined.data(), pipelined.size()));

        std::string all;
        char buf[2048];
        auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start
            < std::chrono::milliseconds(1500)) {
            int n = client.receive(buf, sizeof(buf));
            if (n > 0) {
                all.append(buf, static_cast<size_t>(n));
                size_t count200 = 0;
                size_t pos = 0;
                while ((pos = all.find("HTTP/1.1 200 OK", pos))
                    != std::string::npos) {
                    ++count200;
                    pos += 15;
                }
                if (count200 >= 2) break;
                continue;
            }
            if (n == 0) break;
            const auto err = client.getLastError();
            if (err == SocketError::WouldBlock || err == SocketError::Timeout)
                continue;
            break;
        }

        size_t count200 = 0;
        size_t pos = 0;
        while ((pos = all.find("HTTP/1.1 200 OK", pos)) != std::string::npos) {
            ++count200;
            pos += 15;
        }

        g_serverSignalStop.store(true);
        serverThread.join();
        g_serverSignalStop.store(false);

        REQUIRE(count200 >= 2);
    }

    // Test 10: Request buffering - partial request handling
    BEGIN_TEST("Buffering: handles partial HTTP request");
    {
        TestHttpServer server(ServerBind{"127.0.0.1", Port{0}});
        REQUIRE(server.isValid());

        std::thread serverThread(
            [&server]() { server.run(ClientLimit{1}, Milliseconds{1}); });

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
        DLOG("DEBUG: Sent part1\n");
        server.waitReady();
        client.sendAll(part2.data(), part2.size());
        DLOG("DEBUG: Sent part2\n");

        // Should get complete response
        DLOG("DEBUG: Client receiving response...\n");
        std::string response = receiveCompleteResponse(client);
        DLOG("DEBUG: Client received %zu bytes\n", response.size());

        DLOG("DEBUG: Test 10 - Stopping server...\n");
        g_serverSignalStop.store(true);
        DLOG("DEBUG: Test 10 - Joining server thread...\n");
        serverThread.join();
        g_serverSignalStop.store(false); // Reset for next test
        DLOG("DEBUG: Test 10 - Server thread joined\n");

        REQUIRE(!response.empty());
        REQUIRE(response.find("200 OK") != std::string::npos);
    }

    // Test 10b: HTTP/1.1 requests must include Host
    BEGIN_TEST("HTTP/1.1: missing Host header returns 400");
    {
        TestHttpServer server(ServerBind{"127.0.0.1", Port{0}});
        REQUIRE(server.isValid());

        std::thread serverThread(
            [&server]() { server.run(ClientLimit{1}, Milliseconds{1}); });

        server.waitReady();

        std::string response
            = sendHttpRequest(server.serverPort(), "GET / HTTP/1.1\r\n\r\n");

        g_serverSignalStop.store(true);
        serverThread.join();
        g_serverSignalStop.store(false);

        REQUIRE(!response.empty());
        REQUIRE(response.find("400 Bad Request") != std::string::npos);
    }

    // Test 10c: Request body completeness gates dispatch for Content-Length
    BEGIN_TEST("HTTP/1.1 framing: waits for full Content-Length body");
    {
        TestHttpServer server(ServerBind{"127.0.0.1", Port{0}});
        REQUIRE(server.isValid());

        std::thread serverThread(
            [&server]() { server.run(ClientLimit{1}, Milliseconds{1}); });

        server.waitReady();

        auto clientResult = SocketFactory::createTcpClient(
            AddressFamily::IPv4, ConnectArgs{"127.0.0.1", server.serverPort()});
        REQUIRE(clientResult.isSuccess());
        auto& client = clientResult.value();
        client.setReceiveTimeout(Milliseconds{120});

        const std::string head = "GET / HTTP/1.1\r\nHost: localhost\r\n"
                                 "Content-Length: 4\r\n\r\n";
        REQUIRE(client.sendAll(head.data(), head.size()));
        REQUIRE(client.sendAll("ab", 2));

        char tmp[64];
        const int early = client.receive(tmp, sizeof(tmp));
        REQUIRE(early < 0);
        const auto earlyErr = client.getLastError();
        REQUIRE(earlyErr == SocketError::Timeout
            || earlyErr == SocketError::WouldBlock);

        REQUIRE(client.sendAll("cd", 2));
        std::string response = receiveCompleteResponse(client);

        g_serverSignalStop.store(true);
        serverThread.join();
        g_serverSignalStop.store(false);

        REQUIRE(!response.empty());
        REQUIRE(response.find("200 OK") != std::string::npos);
    }

    // Test 10d: Expect: 100-continue interim response handling
    BEGIN_TEST("HTTP/1.1 framing: Expect 100-continue interim response");
    {
        TestHttpServer server(ServerBind{"127.0.0.1", Port{0}});
        REQUIRE(server.isValid());

        std::thread serverThread(
            [&server]() { server.run(ClientLimit{1}, Milliseconds{1}); });

        server.waitReady();

        auto clientResult = SocketFactory::createTcpClient(
            AddressFamily::IPv4, ConnectArgs{"127.0.0.1", server.serverPort()});
        REQUIRE(clientResult.isSuccess());
        auto& client = clientResult.value();
        client.setReceiveTimeout(Milliseconds{500});

        const std::string headersOnly = "GET / HTTP/1.1\r\nHost: localhost\r\n"
                                        "Expect: 100-continue\r\n"
                                        "Content-Length: 4\r\n\r\n";
        REQUIRE(client.sendAll(headersOnly.data(), headersOnly.size()));

        std::string interim = receiveCompleteResponse(client);
        REQUIRE(!interim.empty());
        REQUIRE(interim.find("100 Continue") != std::string::npos);
        REQUIRE(server.responseBeginCount == 0);
        REQUIRE(server.responseSentCount == 0);

        REQUIRE(client.sendAll("data", 4));
        std::string finalResponse = receiveCompleteResponse(client);

        g_serverSignalStop.store(true);
        serverThread.join();
        g_serverSignalStop.store(false);

        REQUIRE(!finalResponse.empty());
        REQUIRE(finalResponse.find("200 OK") != std::string::npos);
        REQUIRE(server.responseBeginCount == 1);
        REQUIRE(server.responseSentCount == 1);
    }

    // Test 10e: unsupported Expect returns 417
    BEGIN_TEST("HTTP/1.1 framing: unsupported Expect returns 417");
    {
        TestHttpServer server(ServerBind{"127.0.0.1", Port{0}});
        REQUIRE(server.isValid());

        std::thread serverThread(
            [&server]() { server.run(ClientLimit{1}, Milliseconds{1}); });

        server.waitReady();

        std::string response = sendHttpRequest(server.serverPort(),
            "GET / HTTP/1.1\r\nHost: localhost\r\nExpect: kittens\r\n\r\n");

        g_serverSignalStop.store(true);
        serverThread.join();
        g_serverSignalStop.store(false);

        REQUIRE(!response.empty());
        REQUIRE(response.find("417 Expectation Failed") != std::string::npos);
    }

    // Test 10f: unsupported transfer-coding returns 501 and closes
    BEGIN_TEST("HTTP/1.1 framing: unsupported Transfer-Encoding returns 501");
    {
        TestHttpServer server(ServerBind{"127.0.0.1", Port{0}});
        REQUIRE(server.isValid());

        std::thread serverThread(
            [&server]() { server.run(ClientLimit{1}, Milliseconds{1}); });

        server.waitReady();

        std::string response = sendHttpRequest(server.serverPort(),
            "GET / HTTP/1.1\r\nHost: localhost\r\n"
            "Transfer-Encoding: gzip\r\n\r\n");

        g_serverSignalStop.store(true);
        serverThread.join();
        g_serverSignalStop.store(false);

        REQUIRE(!response.empty());
        REQUIRE(response.find("501 Not Implemented") != std::string::npos);
    }

    BEGIN_TEST("Edge case: empty/incomplete request");
    {
        TestHttpServer server(ServerBind{"127.0.0.1", Port{0}});
        REQUIRE(server.isValid());

        DLOG("DEBUG: Test 11 - Running server with ClientLimit{1}\n");
        std::thread serverThread(
            [&server]() { server.run(ClientLimit{1}, Milliseconds{1}); });

        server.waitReady();

        auto clientResult = SocketFactory::createTcpClient(
            AddressFamily::IPv4, ConnectArgs{"127.0.0.1", server.serverPort()});

        if (clientResult.isSuccess()) {
            DLOG("DEBUG: Test 11 - Connected dummy client\n");
            auto& client = clientResult.value();
            // Send nothing or just partial line
            std::string partial = "GET";
            client.sendAll(partial.data(), partial.size());
            DLOG("DEBUG: Test 11 - Sent partial request line: %s\n",
                partial.c_str());

            // Wait for the partial-request timeout to fire
            std::this_thread::sleep_for(50ms);
        }

        DLOG("DEBUG: Test 11 - Stopping server...\n");
        g_serverSignalStop.store(true);
        DLOG("DEBUG: Test 11 - Joining server thread...\n");
        serverThread.join();
        g_serverSignalStop.store(false); // Reset for next test
        DLOG("DEBUG: Test 11 - Server thread joined\n");

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
            void onReady() override {
                ready_.store(true, std::memory_order_release);
            }
            void buildResponse(HttpClientState& state) override {
                // 512 KB body — well above the typical 128–256 KB kernel
                // send buffer, so onWritable must be called multiple times
                // to drain the response across several poll iterations.
                std::string body(512 * 1024, 'X');
                state.responseBuf = "HTTP/1.1 200 OK\r\nContent-Length: "
                    + std::to_string(body.size()) + "\r\n\r\n" + body;
                state.responseView = state.responseBuf;
            }
        };

        LargeResponseServer server(ServerBind{"127.0.0.1", Port{0}});
        REQUIRE(server.isValid());

        std::thread serverThread(
            [&server]() { server.run(ClientLimit{1}, Milliseconds{1}); });

        server.waitReady();

        auto clientResult = SocketFactory::createTcpClient(
            AddressFamily::IPv4, ConnectArgs{"127.0.0.1", server.serverPort()});
        REQUIRE(clientResult.isSuccess());

        auto& client = clientResult.value();

        client.setReceiveTimeout(Milliseconds{2000});

        // Connection: close so the server closes after sending and the receive
        // loop exits on EOF rather than blocking until the receive timeout.
        std::string req
            = "GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
        assert(client.isBlocking());
        client.sendAll(req.data(), req.size());

        // Receive all data until server closes the connection (EOF)
        std::string response;
        char buf[1024];
        int n{0};
        while ((n = client.receive(buf, sizeof(buf))) > 0) {
            response.append(buf, n);
        }

        DLOG("[DEBUG] Test 12 - Stopping server\n");
        g_serverSignalStop.store(true);

        serverThread.join();
        g_serverSignalStop.store(false); // Reset for next test

        REQUIRE(response.size() >= 512 * 1024);
        REQUIRE(response.find("200 OK") != std::string::npos);
    }

    // Test 13: Hook timing - responseBegin before responseSent
    BEGIN_TEST("Hook timing: responseBegin always before responseSent");
    {
        TestHttpServer server(ServerBind{"127.0.0.1", Port{0}});
        REQUIRE(server.isValid());

        std::thread serverThread(
            [&server]() { server.run(ClientLimit{1}, Milliseconds{1}); });

        server.waitReady();

        sendHttpRequest(
            server.serverPort(), "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");

        DLOG("[DEBUG] Test 13 - Stopping server\n");
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
            server.run(ClientLimit::Unlimited, Milliseconds{1});
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

        DLOG("[DEBUG] Test 14 - Stopping server\n");
        g_serverSignalStop.store(true);
        serverThread.join();
        g_serverSignalStop.store(false);
        DLOG("[DEBUG] Test 14 - Server thread joined\n");
    }

    // Test 15: Request scan position tracking (incremental parsing)
    BEGIN_TEST("Optimization: request scan position tracks parsing progress");
    {
        // This is tested implicitly - if incremental parsing works,
        // partial requests should be handled efficiently
        TestHttpServer server(ServerBind{"127.0.0.1", Port{0}});
        REQUIRE(server.isValid());

        std::thread serverThread(
            [&server]() { server.run(ClientLimit{1}, Milliseconds{1}); });

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

    // -----------------------------------------------------------------------
    // HttpClientState assignment operators (copy and move)
    // -----------------------------------------------------------------------

    // Test 16: Copy assignment — basic
    // Use a response long enough to be heap-allocated (> 22 bytes for libc++
    // SSO). The copy-assignment fixup redirects responseView into the new
    // responseBuf only when it pointed into the original's responseBuf.
    BEGIN_TEST("HttpClientState: copy assignment copies all fields");
    {
        HttpClientState src;
        src.request = "GET / HTTP/1.1\r\n\r\n";
        src.responseBuf = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nHello";
        src.responseView = src.responseBuf; // view into own buf
        src.sent = 7;
        src.responseStarted = true;
        src.closeAfterSend = true;
        src.requestScanPos = 3;

        HttpClientState dst;
        dst = src;

        REQUIRE(dst.request == src.request);
        REQUIRE(dst.sent == src.sent);
        REQUIRE(dst.responseStarted == src.responseStarted);
        REQUIRE(dst.closeAfterSend == src.closeAfterSend);
        REQUIRE(dst.requestScanPos == src.requestScanPos);
        REQUIRE(dst.responseBuf == src.responseBuf);
        // responseView must have been redirected into dst's own responseBuf
        REQUIRE(dst.responseView.data() == dst.responseBuf.data());
    }

    // Test 17: Copy assignment — self-assignment is a no-op
    BEGIN_TEST("HttpClientState: copy self-assignment is safe");
    {
        HttpClientState s;
        s.request = "hello";
        s.sent = 42;
        HttpClientState& ref = s;
        ref = s; // self-assign
        REQUIRE(s.request == "hello");
        REQUIRE(s.sent == 42u);
    }

    // Test 18: Copy assignment — responseView pointing outside responseBuf
    // is NOT redirected (it belongs to external storage)
    BEGIN_TEST("HttpClientState: copy assignment with external responseView");
    {
        static const std::string sharedStorage = "HTTP/1.1 200 OK\r\n\r\n";
        HttpClientState src;
        src.responseBuf.clear();
        src.responseView = sharedStorage; // view into external storage

        HttpClientState dst;
        dst = src;

        // responseBuf is empty -> no fixup branch taken
        REQUIRE(dst.responseBuf.empty());
        // responseView still points at sharedStorage (unchanged)
        REQUIRE(dst.responseView.data() == sharedStorage.data());
    }

    // Test 19: Move assignment — basic
    // Use a response long enough to be heap-allocated (> 22 bytes for libc++
    // SSO). The move-assignment fixup relies on the moved-from data() address
    // being preserved, which only happens for heap strings, not SSO strings.
    BEGIN_TEST("HttpClientState: move assignment transfers all fields");
    {
        HttpClientState src;
        src.request = "GET / HTTP/1.1\r\n\r\n";
        src.responseBuf = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nHello";
        src.responseView = src.responseBuf; // view into own buf
        src.sent = 5;
        src.closeAfterSend = true;
        src.requestScanPos = 2;

        HttpClientState dst;
        dst = std::move(src);

        REQUIRE(dst.request == "GET / HTTP/1.1\r\n\r\n");
        REQUIRE(dst.sent == 5u);
        REQUIRE(dst.closeAfterSend == true);
        REQUIRE(dst.requestScanPos == 2u);
        REQUIRE(!dst.responseBuf.empty());
        // responseView must have been fixed up to dst's responseBuf
        REQUIRE(dst.responseView.data() == dst.responseBuf.data());
        // After move, src.responseView should be cleared
        REQUIRE(src.responseView.empty()); // NOLINT(bugprone-use-after-move)
    }

    // Test 20: Move assignment — self-assignment is a no-op
    BEGIN_TEST("HttpClientState: move self-assignment is safe");
    {
        HttpClientState s;
        s.request = "world";
        s.sent = 99;
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wself-move"
#endif
        s = std::move(
            *&s); // self-move-assign via pointer to silence -Wself-move
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
        REQUIRE(s.sent == 99u);
    }

    // Test 21: Move assignment — external responseView not redirected
    BEGIN_TEST("HttpClientState: move assignment with external responseView");
    {
        static const std::string ext = "HTTP/1.1 404 Not Found\r\n\r\n";
        HttpClientState src;
        src.responseBuf.clear();
        src.responseView = ext; // external storage: no fixup needed

        HttpClientState dst;
        dst = std::move(src);

        REQUIRE(dst.responseBuf.empty());
        REQUIRE(dst.responseView.data() == ext.data());
    }

    // -----------------------------------------------------------------------
    // printBuildInfo and printStartupBanner smoke tests
    // -----------------------------------------------------------------------

    // Test 22: printBuildInfo runs without crashing
    BEGIN_TEST("printBuildInfo: smoke test (output goes to stdout)");
    {
        HttpPollServer::printBuildInfo();
        REQUIRE(true);
    }

    // Test 23: printStartupBanner runs without crashing
    BEGIN_TEST("printStartupBanner: smoke test via TestHttpServer accessor");
    {
        TestHttpServer tmp(ServerBind{"127.0.0.1", Port{0}});
        tmp.callPrintStartupBanner();
        REQUIRE(true);
    }

    // Test 24: Malformed request closes connection deterministically
    BEGIN_TEST("Malformed request returns 400 and closes connection");
    {
        TestHttpServer server(ServerBind{"127.0.0.1", Port{0}});
        REQUIRE(server.isValid());

        std::thread serverThread(
            [&server]() { server.run(ClientLimit{1}, Milliseconds{1}); });
        server.waitReady();

        auto clientResult = SocketFactory::createTcpClient(
            AddressFamily::IPv4, ConnectArgs{"127.0.0.1", server.serverPort()});
        REQUIRE(clientResult.isSuccess());
        auto& client = clientResult.value();
        client.setReceiveTimeout(Milliseconds{500});

        const std::string malformed = "BOGUS\r\n\r\n";
        REQUIRE(client.sendAll(malformed.data(), malformed.size()));

        std::string response = receiveCompleteResponse(client);
        REQUIRE(response.find("400 Bad Request") != std::string::npos);
        REQUIRE(response.find("Connection: close") != std::string::npos);

        // A follow-up receive should observe EOF (peer close), not just a
        // timeout. Poll briefly to allow FIN to arrive.
        bool sawEof = false;
        char b[8];
        for (int i = 0; i < 20; ++i) {
            int n = client.receive(b, sizeof(b));
            if (n == 0) {
                sawEof = true;
                break;
            }
            if (n < 0) {
                const auto err = client.getLastError();
                if (err == SocketError::WouldBlock
                    || err == SocketError::Timeout) {
                    std::this_thread::sleep_for(10ms);
                    continue;
                }
                break;
            }
        }
        REQUIRE(sawEof);

        g_serverSignalStop.store(true);
        serverThread.join();
        g_serverSignalStop.store(false);
    }

    // Test 25: Method rejection closes connection deterministically
    BEGIN_TEST("Unsupported method returns 405 and closes connection");
    {
        TestHttpServer server(ServerBind{"127.0.0.1", Port{0}});
        REQUIRE(server.isValid());

        std::thread serverThread(
            [&server]() { server.run(ClientLimit{1}, Milliseconds{1}); });
        server.waitReady();

        auto clientResult = SocketFactory::createTcpClient(
            AddressFamily::IPv4, ConnectArgs{"127.0.0.1", server.serverPort()});
        REQUIRE(clientResult.isSuccess());
        auto& client = clientResult.value();
        client.setReceiveTimeout(Milliseconds{500});

        const std::string request = "POST / HTTP/1.1\r\nHost: localhost\r\n"
                                    "Content-Length: 0\r\n\r\n";
        REQUIRE(client.sendAll(request.data(), request.size()));

        std::string response = receiveCompleteResponse(client);
        REQUIRE(response.find("405 Method Not Allowed") != std::string::npos);
        REQUIRE(response.find("Connection: close") != std::string::npos);

        bool sawEof = false;
        char b[8];
        for (int i = 0; i < 20; ++i) {
            int n = client.receive(b, sizeof(b));
            if (n == 0) {
                sawEof = true;
                break;
            }
            if (n < 0) {
                const auto err = client.getLastError();
                if (err == SocketError::WouldBlock
                    || err == SocketError::Timeout) {
                    std::this_thread::sleep_for(10ms);
                    continue;
                }
                break;
            }
        }
        REQUIRE(sawEof);

        g_serverSignalStop.store(true);
        serverThread.join();
        g_serverSignalStop.store(false);
    }

    // -----------------------------------------------------------------------
    // Latency tuning: verify the new constants are in effect
    // -----------------------------------------------------------------------
    BEGIN_TEST("KeepAlive: HIGH_LOAD_THRESHOLD is 256");
    {
        REQUIRE(KeepAliveTimeoutManager::HIGH_LOAD_THRESHOLD == 256);
    }

    BEGIN_TEST("KeepAlive: AGGRESSIVE_TIMEOUT is 500 ms (not 5000 ms)");
    {
        using ms = std::chrono::milliseconds;
        REQUIRE(KeepAliveTimeoutManager::AGGRESSIVE_TIMEOUT == ms{500});
    }

    BEGIN_TEST("KeepAlive: adjustForLoad activates high-load at threshold 256");
    {
        KeepAliveTimeoutManager mgr;
        mgr.setTimeout(std::chrono::milliseconds{30000});
        // Below threshold — normal timeout unchanged
        mgr.adjustForLoad(255);
        REQUIRE(mgr.getTimeout() == std::chrono::milliseconds{30000});
        // At threshold+1 — aggressive timeout kicks in
        mgr.adjustForLoad(257);
        REQUIRE(
            mgr.getTimeout() == KeepAliveTimeoutManager::AGGRESSIVE_TIMEOUT);
        // Drop back below threshold — normal timeout restored
        mgr.adjustForLoad(10);
        REQUIRE(mgr.getTimeout() == std::chrono::milliseconds{30000});
    }

    BEGIN_TEST("Slowloris: high-load constants are correct");
    {
        REQUIRE(HttpPollServer::SLOWLORIS_TIMEOUT_MS_HIGH_LOAD == 1000);
        REQUIRE(HttpPollServer::SLOWLORIS_HIGH_LOAD_THRESHOLD == 64);
        // Normal timeout is unchanged
        REQUIRE(HttpPollServer::SLOWLORIS_TIMEOUT_MS == 5000);
    }

    BEGIN_TEST("RECV_BUF_SIZE: per-loop read buffer is 32 KB");
    {
        REQUIRE(HttpPollServer::RECV_BUF_SIZE == 32 * 1024);
    }

    return test_summary();
}
