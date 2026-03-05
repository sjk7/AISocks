// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Simple tests for HttpPollServer - basic instantiation and API

#include "HttpPollServer.h"
#include "test_helpers.h"
#include <iostream>

using namespace aiSocks;

// Test server with instrumented hooks
class TestHttpServer : public HttpPollServer {
    public:
    int responseBeginCount = 0;
    int responseSentCount = 0;

    explicit TestHttpServer(const ServerBind& bind) : HttpPollServer(bind) {}

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
};

int main() {
    std::cout << "=== HttpPollServer Tests ===\n";

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

    // Test 3: getActualPort works
    BEGIN_TEST("API: getActualPort returns bound port");
    {
        TestHttpServer server(ServerBind{"127.0.0.1", Port{0}});
        Port port = server.getActualPort();
        REQUIRE(port.value() > 0); // Should have bound to a dynamic port
    }

    return test_summary();
}
