// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it. PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#include "HttpRequest.h"
#include "HttpPollServer.h"
#include "TcpSocket.h"
#include "test_helpers.h"
#include <string>
#include <string_view>
#include <thread>
#include <chrono>

using namespace aiSocks;

/**
 * @brief Tests for unhappy HTTP scenarios: truncated headers,
 * invalid hex encoding, oversized headers, and fast-timeout Slowloris.
 */

class MockHttpServer : public HttpPollServer {
    public:
    std::atomic<bool> disconnected{false};
    std::atomic<bool> readyFlag{false};

    explicit MockHttpServer(const ServerBind& bind) : HttpPollServer(bind) {}

    void buildResponse(HttpClientState&) override {
        // Not used for Slowloris test
    }

    void onDisconnect(HttpClientState&) override { disconnected = true; }

    void onReady() override { readyFlag = true; }

    void waitReady() {
        while (!readyFlag) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    // Override onIdle to force the loop to continue even if no events
    ServerResult onIdle() override { return ServerResult::KeepConnection; }
};

static void test_slowloris_protection() {
    BEGIN_TEST("slowloris protection (5s timeout)");

    MockHttpServer server(ServerBind{"127.0.0.1", Port{0}});
    REQUIRE(server.isValid());
    Port port = server.serverPort();

    // Start server in background thread
    std::thread serverThread([&]() {
        // Use a short timeout to ensure onIdle and checks run frequently
        server.run(ClientLimit::Default, Milliseconds{10});
    });

    server.waitReady();

    // Connect and send partial header
    TcpSocket client(AddressFamily::IPv4, ConnectArgs{"127.0.0.1", port});
    REQUIRE(client.isValid());

    std::string partial = "GET / HTTP/1.1\r\nHost: localhost\r\n";
    client.send(partial.data(), partial.size());

    // Wait for > 5000ms (SLOWLORIS_TIMEOUT_MS)
    std::this_thread::sleep_for(std::chrono::milliseconds(5200));

    // We need to trigger an event to wake up the server or rely on the poll
    // timeout. Since we used Milliseconds{10}, it should wake up frequently.
    // However, the check is inside onReadable.
    // Let's send one more byte to trigger onReadable if it hasn't timed out.
    char extra = 'X';
    client.send(&extra, 1);

    // Give it a moment to process
    int retries = 40;
    while (!server.disconnected && retries-- > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    REQUIRE(server.disconnected.load() == true);

    server.requestStop();
    serverThread.join();
}

static void test_invalid_percent_encoding() {
    BEGIN_TEST("invalid percent encoding");

    // Truncated percent
    auto req1 = HttpRequest::parse("GET /foo%%1 HTTP/1.1\r\n\r\n");
    REQUIRE(req1.path == "/foo%%1"); // Should pass through verbatim

    // Invalid hex
    auto req2 = HttpRequest::parse("GET /foo%%G1 HTTP/1.1\r\n\r\n");
    REQUIRE(req2.path == "/foo%%G1");

    // Multiple invalid
    auto req3 = HttpRequest::parse("GET /%%ZZ/%%1/%%AG HTTP/1.1\r\n\r\n");
    REQUIRE(req3.path == "/%%ZZ/%%1/%%AG");
}

static void test_binary_data_in_headers() {
    BEGIN_TEST("binary data in headers");

    // Header values containing null bytes or non-ASCII
    std::string raw = "GET / HTTP/1.1\r\n";
    raw += "X-Binary: ";
    raw.push_back('\0');
    raw.push_back('\1');
    raw.push_back('\2');
    raw += "\r\n";
    raw += "X-UTF8: \xF0\x9F\x91\xBE\r\n\r\n";

    // String content for raw has a null, so we must use string_view carefully
    auto req = HttpRequest::parse(std::string_view(raw.data(), raw.size()));

    auto bin = req.header("x-binary");
    REQUIRE(bin != nullptr);
    REQUIRE(bin->size() == 3);
    REQUIRE((*bin)[0] == '\0');

    auto utf8 = req.header("x-utf8");
    REQUIRE(utf8 != nullptr);
    REQUIRE(*utf8 == "\xF0\x9F\x91\xBE");
}

static void test_malformed_header_lines() {
    BEGIN_TEST("malformed header lines");

    // Line without colon
    auto req = HttpRequest::parse("GET / HTTP/1.1\r\n"
                                  "No-Colon-Line\r\n"
                                  "Valid-Header: yes\r\n\r\n");
    REQUIRE(req.valid);
    REQUIRE(req.headers.size() == 1);
    REQUIRE(req.headerOr("valid-header") == "yes");
}

static void test_oversized_header_parse() {
    BEGIN_TEST("oversized header parse (8KB benchmark)");

    // Verify HttpRequest::parse behavior with a 1MB input.
    // We keep the large parsing test to ensure the parser itself isn't 
    // artificially limited, even if the server is.
    std::string longHeader(1024 * 1024, 'a');
    std::string raw = "GET / HTTP/1.1\r\n"
                      "X-Long: "
        + longHeader + "\r\n\r\n";

    auto req = HttpRequest::parse(raw);
    REQUIRE(req.valid);
    REQUIRE(req.headerOr("x-long").size() == longHeader.size());
}

static void test_server_enforces_header_limit() {
    BEGIN_TEST("server enforces header limit (8KB)");

    MockHttpServer server(ServerBind{"127.0.0.1", Port{0}});
    REQUIRE(server.isValid());
    Port port = server.serverPort();

    std::thread serverThread([&]() {
        server.run(ClientLimit::Default, Milliseconds{10});
    });

    server.waitReady();

    TcpSocket client(AddressFamily::IPv4, ConnectArgs{"127.0.0.1", port});
    REQUIRE(client.isValid());

    // Send exactly 1 byte more than the limit
    std::string oversized(HttpPollServer::MAX_HEADER_SIZE + 1, 'X');
    client.send(oversized.data(), oversized.size());

    // Server should send 431 and closed the connection
    char response[1024];
    int n = client.receive(response, sizeof(response) - 1);
    
    if (n > 0) {
        response[n] = '\0';
        std::string respStr(response);
        REQUIRE(respStr.find("431 Request Header Fields Too Large") != std::string::npos);
    }
    
    // Connection should be closed by server or closing after write
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Attempting to send again should fail or be ignored
    char extra = 'Y';
    client.send(&extra, 1);
    
    server.requestStop();
    serverThread.join();
}

int main() {
    g_totalTimer.reset();
    test_invalid_percent_encoding();
    test_binary_data_in_headers();
    test_malformed_header_lines();
    test_oversized_header_parse();
    test_server_enforces_header_limit();
    test_slowloris_protection();
    return test_summary();
}
