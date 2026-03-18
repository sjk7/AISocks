// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Simple tests for HttpPollServer - basic instantiation and API

#include "HttpPollServer.h"
#include "SocketFactory.h"
#include "TcpSocket.h"
#include "test_helpers.h"
#include <atomic>
#include <chrono>
#include <string>
#include <thread>

using namespace aiSocks;

// Test server with instrumented hooks
class TestHttpServer : public HttpPollServer {
    public:
    int responseBeginCount = 0;
    int responseSentCount = 0;
    std::atomic<bool> ready_{false};

    explicit TestHttpServer(const ServerBind& bind) : HttpPollServer(bind) {}

    void waitReady() {
        while (!ready_.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
    }

    protected:
    void buildResponse(HttpClientState& state) override {
        state.responseBuf = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK";
        state.responseView = state.responseBuf;
    }

    void onResponseBegin(HttpClientState& state) override {
        responseBeginCount++;
        HttpPollServer::onResponseBegin(state);
    }

    void onResponseSent(HttpClientState& state) override {
        responseSentCount++;
        HttpPollServer::onResponseSent(state);
    }

    void onReady() override { ready_.store(true, std::memory_order_release); }
};

static std::string sendSimpleRequest(Port port, const std::string& request) {
    auto clientResult = SocketFactory::createTcpClient(
        AddressFamily::IPv4, ConnectArgs{"127.0.0.1", port, Milliseconds{500}});
    if (!clientResult.isSuccess()) return {};

    auto& client = clientResult.value();
    client.setReceiveTimeout(Milliseconds{500});
    if (!client.sendAll(request.data(), request.size())) return {};

    std::string response;
    char buf[512];
    int n = client.receive(buf, sizeof(buf));
    if (n > 0) {
        response.assign(buf, static_cast<size_t>(n));
    }
    client.close();
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

    // Test 3: serverPort works
    BEGIN_TEST("API: serverPort returns bound port");
    {
        TestHttpServer server(ServerBind{"127.0.0.1", Port{0}});
        Port port = server.serverPort();
        REQUIRE(port.value() > 0); // Should have bound to a dynamic port
    }

    // Test 4: end-to-end request handling + hook invocation
    BEGIN_TEST("Integration: serves HTTP response and fires response hooks");
    {
        TestHttpServer server(ServerBind{"127.0.0.1", Port{0}});
        REQUIRE(server.isValid());
        const Port port = server.serverPort();
        REQUIRE(port.value() > 0);

        std::thread serverThread([&server]() { server.run(ClientLimit{4}, Milliseconds{1}); });
        server.waitReady();

        const std::string request = "GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
        const std::string response1 = sendSimpleRequest(port, request);
        REQUIRE(!response1.empty());
        REQUIRE(response1.find("HTTP/1.1 200 OK") != std::string::npos);
        REQUIRE(response1.find("\r\n\r\nOK") != std::string::npos);

        const std::string response2 = sendSimpleRequest(port, request);
        REQUIRE(!response2.empty());
        REQUIRE(response2.find("HTTP/1.1 200 OK") != std::string::npos);

        auto start = std::chrono::steady_clock::now();
        while ((server.responseBeginCount < 2 || server.responseSentCount < 2)
            && std::chrono::steady_clock::now() - start
                < std::chrono::milliseconds{800}) {
            std::this_thread::sleep_for(std::chrono::milliseconds{2});
        }
        REQUIRE(server.responseBeginCount >= 2);
        REQUIRE(server.responseSentCount >= 2);

        server.requestStop();
        serverThread.join();
    }

    return test_summary();
}
