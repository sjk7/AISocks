// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Integration tests: HttpClient talking to a real HttpPollServer in-process.
//
// These tests exercise the full request/response path — TCP connect, HTTP
// framing, response parsing, keep-alive — without any network mocking.
// Each test spins up the server on a background thread, uses the server's
// own requestStop() API for clean shutdown, and synchronises on a
// condition_variable (via waitReady()) to eliminate startup races.

#include "HttpClient.h"
#include "HttpPollServer.h"
#include "test_helpers.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

using namespace aiSocks;
using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// EchoServer — responds with 200 OK and a body that mirrors the request path
// ---------------------------------------------------------------------------
class EchoServer : public HttpPollServer {
    public:
    explicit EchoServer() : HttpPollServer(ServerBind{"127.0.0.1", Port{0}}) {
        setHandleSignals(false);
    }
    explicit EchoServer(ServerBind bind) : HttpPollServer(std::move(bind)) {
        setHandleSignals(false);
    }

    // Block until onReady() has fired and the server is in its poll loop.
    void waitReady() {
        std::unique_lock<std::mutex> lk(readyMtx_);
        readyCv_.wait(lk, [this] { return ready_.load(); });
    }

    std::atomic<int> requestsServed{0};

    protected:
    void onReady() override {
        {
            std::lock_guard<std::mutex> lk(readyMtx_);
            ready_ = true;
        }
        readyCv_.notify_all();
    }

    void buildResponse(HttpClientState& s) override {
        // Extract path from the stored request line (e.g. "GET /hello
        // HTTP/1.1")
        std::string_view req = s.request;
        std::string path;
        auto spaceAfterMethod = req.find(' ');
        if (spaceAfterMethod != std::string_view::npos) {
            auto pathStart = spaceAfterMethod + 1;
            auto pathEnd = req.find(' ', pathStart);
            path = std::string(req.substr(pathStart,
                pathEnd == std::string_view::npos ? std::string_view::npos
                                                  : pathEnd - pathStart));
        }

        s.responseBuf = makeResponse(
            "HTTP/1.1 200 OK", "text/plain", path.empty() ? "/" : path);
        s.responseView = s.responseBuf;
        requestsServed.fetch_add(1, std::memory_order_relaxed);
    }

    private:
    std::atomic<bool> ready_{false};
    std::mutex readyMtx_;
    std::condition_variable readyCv_;
};

// ---------------------------------------------------------------------------
// SilentServer — accepts connections and drains input but never writes back.
// Used to test request-timeout: client's receive deadline fires first.
// ---------------------------------------------------------------------------
class SilentServer : public HttpPollServer {
    public:
    explicit SilentServer() : HttpPollServer(ServerBind{"127.0.0.1", Port{0}}) {
        setHandleSignals(false);
    }

    void waitReady() {
        std::unique_lock<std::mutex> lk(readyMtx_);
        readyCv_.wait(lk, [this] { return ready_.load(); });
    }

    protected:
    void onReady() override {
        {
            std::lock_guard<std::mutex> lk(readyMtx_);
            ready_ = true;
        }
        readyCv_.notify_all();
    }

    // Leave responseView empty: onWritable returns KeepConnection when
    // responseView is empty, so the socket stays open indefinitely.
    // The client's requestTimeout fires rather than waiting forever.
    void buildResponse(HttpClientState& /*s*/) override {}

    private:
    std::atomic<bool> ready_{false};
    std::mutex readyMtx_;
    std::condition_variable readyCv_;
};

// ---------------------------------------------------------------------------
// RedirectToHttpsServer — always returns a redirect to an https URL.
// Used to verify client behavior for unsupported https redirects.
// ---------------------------------------------------------------------------
class RedirectToHttpsServer : public HttpPollServer {
    public:
    explicit RedirectToHttpsServer()
        : HttpPollServer(ServerBind{"127.0.0.1", Port{0}}) {
        setHandleSignals(false);
    }

    void waitReady() {
        std::unique_lock<std::mutex> lk(readyMtx_);
        readyCv_.wait(lk, [this] { return ready_.load(); });
    }

    protected:
    void onReady() override {
        {
            std::lock_guard<std::mutex> lk(readyMtx_);
            ready_ = true;
        }
        readyCv_.notify_all();
    }

    void buildResponse(HttpClientState& s) override {
        s.responseBuf = "HTTP/1.1 302 Found\r\n"
                        "Location: https://example.com/secure\r\n"
                        "Content-Length: 0\r\n"
                        "Connection: close\r\n\r\n";
        s.responseView = s.responseBuf;
    }

    private:
    std::atomic<bool> ready_{false};
    std::mutex readyMtx_;
    std::condition_variable readyCv_;
};

// ---------------------------------------------------------------------------
// ReuseTrackingServer — serves keep-alive responses and tracks accepted
// connections and dispatched requests.
// ---------------------------------------------------------------------------
class ReuseTrackingServer : public HttpPollServer {
    public:
    explicit ReuseTrackingServer()
        : HttpPollServer(ServerBind{"127.0.0.1", Port{0}}) {
        setHandleSignals(false);
    }

    void waitReady() {
        std::unique_lock<std::mutex> lk(readyMtx_);
        readyCv_.wait(lk, [this] { return ready_.load(); });
    }

    std::atomic<int> connectionsAccepted{0};
    std::atomic<int> requestsServed{0};

    protected:
    void onReady() override {
        {
            std::lock_guard<std::mutex> lk(readyMtx_);
            ready_ = true;
        }
        readyCv_.notify_all();
    }

    void onClientConnected(TcpSocket& sock, HttpClientState& s) override {
        connectionsAccepted.fetch_add(1, std::memory_order_relaxed);
        HttpPollServer::onClientConnected(sock, s);
    }

    void buildResponse(HttpClientState& s) override {
        s.responseBuf = "HTTP/1.1 200 OK\r\n"
                        "Content-Length: 2\r\n"
                        "Connection: keep-alive\r\n\r\n"
                        "OK";
        s.responseView = s.responseBuf;
        requestsServed.fetch_add(1, std::memory_order_relaxed);
    }

    private:
    std::atomic<bool> ready_{false};
    std::mutex readyMtx_;
    std::condition_variable readyCv_;
};

// ---------------------------------------------------------------------------
// RAII helper: starts the server on construction, stops+joins on destruction
// ---------------------------------------------------------------------------
struct ServerGuard {
    EchoServer server;
    std::thread thread;

    ServerGuard() {
        thread = std::thread(
            [this] { server.run(ClientLimit::Unlimited, Milliseconds{1}); });
        server.waitReady();
        // Startup should never fail with Port{0} — catch broken test setups.
        if (!server.isValid()) {
            thread.join();
            throw std::runtime_error("ServerGuard: server failed to start");
        }
    }

    ~ServerGuard() {
        server.requestStop();
        thread.join();
    }

    // Convenience: return an HttpClient pre-configured to talk to this server.
    HttpClient makeClient() {
        HttpClient::Options opts;
        opts.connectTimeout = Milliseconds{500};
        opts.requestTimeout = Milliseconds{500};
        return HttpClient{opts};
    }

    std::string baseUrl() const {
        return "http://127.0.0.1:"
            + std::to_string(server.serverPort().value());
    }
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

// All happy-path tests share one ServerGuard to pay the spin-up cost once.
static void run_happy_path_tests(ServerGuard& sg) {
    auto client = sg.makeClient();

    BEGIN_TEST("HttpClient GET to HttpPollServer returns 200 OK");
    {
        auto r = client.get(sg.baseUrl() + "/hello");
        REQUIRE(r.isSuccess());
        if (!r.isSuccess()) return;
        REQUIRE(r.value().statusCode() == 200);
        REQUIRE(r.value().body().find("/hello") != std::string_view::npos);
    }

    BEGIN_TEST("HttpClient GET: echo server body contains request path");
    {
        auto r = client.get(sg.baseUrl() + "/ping");
        REQUIRE(r.isSuccess());
        if (!r.isSuccess()) return;
        REQUIRE(r.value().body().find("/ping") != std::string_view::npos);
    }

    BEGIN_TEST("HttpClient: multiple sequential GETs to same server succeed");
    {
        for (int i = 0; i < 3; ++i) {
            auto r = client.get(sg.baseUrl() + "/item/" + std::to_string(i));
            REQUIRE(r.isSuccess());
            if (!r.isSuccess()) return;
            REQUIRE(r.value().statusCode() == 200);
        }
        REQUIRE(sg.server.requestsServed.load() >= 3);
    }

    BEGIN_TEST("HttpClient POST to HttpPollServer returns 405 (GET/HEAD only)");
    {
        auto r = client.post(sg.baseUrl() + "/submit", "key=value");
        REQUIRE(r.isSuccess());
        if (!r.isSuccess()) return;
        REQUIRE(r.value().statusCode() == 405);
    }
}

static void test_invalid_host_returns_failure() {
    BEGIN_TEST("HttpClient GET to refused port returns failure");

    HttpClient::Options opts;
    opts.connectTimeout = Milliseconds{150};
    opts.requestTimeout = Milliseconds{150};
    HttpClient client{opts};

    auto result = client.get("http://127.0.0.1:1"); // port 1 should refuse

    REQUIRE(!result.isSuccess());
}

static void test_bad_dns_fails() {
    BEGIN_TEST("HttpClient GET to unresolvable hostname returns failure");

    // .invalid is reserved by RFC 2606 and guaranteed never to resolve.
    HttpClient::Options opts;
    opts.connectTimeout = Milliseconds{150};
    HttpClient client{opts};

    auto result = client.get("http://this.host.does.not.exist.invalid/");

    REQUIRE(!result.isSuccess());
}

static void test_connect_timeout_fails() {
    BEGIN_TEST("HttpClient GET with short connect timeout to black-hole "
               "returns failure");

    // 192.0.2.0/24 is TEST-NET-1 (RFC 5737): documentation-only block,
    // guaranteed not to respond.  On some networks the OS may return
    // ENETUNREACH immediately (instant failure); on others the SYN enters
    // a black hole and we rely on the connect timeout.  requestTimeout is
    // also kept short so that even in the unlikely case the TCP handshake
    // "succeeds" and a request is sent, we do not block for the default
    // 60 s receive timeout.
    HttpClient::Options opts;
    opts.connectTimeout = Milliseconds{150};
    opts.requestTimeout = Milliseconds{150};
    HttpClient client{opts};

    auto result = client.get("http://192.0.2.1/");

    REQUIRE(!result.isSuccess());
}

static void test_request_timeout_fails() {
    BEGIN_TEST(
        "HttpClient GET times out when server accepts but never responds");

    // SilentServer drains the HTTP request but never writes a response
    // (onReadable returns KeepConnection, no dispatchBuildResponse call).
    // The client's 200 ms requestTimeout fires first → receive returns an
    // error and performRequest returns failure.
    SilentServer server;
    std::thread serverThread(
        [&] { server.run(ClientLimit::Unlimited, Milliseconds{1}); });
    server.waitReady();

    HttpClient::Options opts;
    opts.connectTimeout = Milliseconds{500};
    opts.requestTimeout = Milliseconds{150};
    auto result = HttpClient{opts}.get("http://127.0.0.1:"
        + std::to_string(server.serverPort().value()) + "/");

    server.requestStop();
    serverThread.join();

    REQUIRE(!result.isSuccess());
}

static void test_request_timeout_is_total_deadline() {
    BEGIN_TEST("HttpClient requestTimeout applies to full response deadline");

    auto listenerResult = SocketFactory::createTcpServer(
        AddressFamily::IPv4, ServerBind{"127.0.0.1", Port{0}});
    REQUIRE(listenerResult.isSuccess());
    if (!listenerResult.isSuccess()) return;
    TcpSocket listener = std::move(listenerResult.value());
    REQUIRE(listener.setReceiveTimeout(Milliseconds{500}));

    const Port port = listener.getLocalEndpoint().value().port;

    std::thread serverThread([&] {
        auto client = listener.accept();
        if (!client) return;

        client->setReceiveTimeout(Milliseconds{500});
        char reqBuf[1024];
        (void)client->receive(reqBuf, sizeof(reqBuf)); // drain request bytes

        const std::string hdr = "HTTP/1.1 200 OK\r\n"
                                "Content-Length: 5\r\n"
                                "Connection: close\r\n\r\n";
        if (!client->sendAll(hdr.data(), hdr.size())) return;

        const char* body = "hello";
        for (int i = 0; i < 5; ++i) {
            if (!client->sendAll(body + i, 1)) break;
            std::this_thread::sleep_for(80ms);
        }
    });

    HttpClient::Options opts;
    opts.connectTimeout = Milliseconds{500};
    opts.requestTimeout = Milliseconds{150};

    auto result = HttpClient{opts}.get(
        "http://127.0.0.1:" + std::to_string(port.value()) + "/");

    serverThread.join();
    REQUIRE(!result.isSuccess());
}

static void test_server_init_failure_unblocks_waiter() {
    BEGIN_TEST("waitReady() unblocks and isValid() returns false when server "
               "fails to start");

    // Occupy a port with a real server so a second bind to the same port fails.
    EchoServer occupant;
    std::thread occupantThread(
        [&] { occupant.run(ClientLimit::Unlimited, Milliseconds{1}); });
    occupant.waitReady();
    REQUIRE(occupant.isValid());
    const Port takenPort = occupant.serverPort();

    // Construct a server targeting the already-bound port with reuseAddr=false
    // so the bind fails at construction time — isValid() is false before run().
    EchoServer failing{
        ServerBind{"127.0.0.1", takenPort, Backlog{}, false, false}};
    REQUIRE(!failing.isValid());

    // run() on a thread must call onReady() and exit immediately.
    std::atomic<bool> done{false};
    std::thread failThread([&] {
        failing.run(ClientLimit::Unlimited, Milliseconds{1});
        done.store(true, std::memory_order_release);
    });

    // waitReady() must return promptly — if it can hang, this test hangs.
    failing.waitReady();
    REQUIRE(!failing.isValid());

    failThread.join(); // must not block
    REQUIRE(done.load());

    occupant.requestStop();
    occupantThread.join();
}

static void test_interim_100_response_is_ignored() {
    BEGIN_TEST("HttpClient ignores interim 100 Continue and returns final");

    auto listenerResult = SocketFactory::createTcpServer(
        AddressFamily::IPv4, ServerBind{"127.0.0.1", Port{0}});
    REQUIRE(listenerResult.isSuccess());
    if (!listenerResult.isSuccess()) return;
    TcpSocket listener = std::move(listenerResult.value());
    REQUIRE(listener.setReceiveTimeout(Milliseconds{500}));

    const Port port = listener.getLocalEndpoint().value().port;

    std::thread serverThread([&] {
        auto client = listener.accept();
        if (!client) return;
        client->setReceiveTimeout(Milliseconds{500});

        char reqBuf[1024];
        (void)client->receive(reqBuf, sizeof(reqBuf));

        const std::string interim = "HTTP/1.1 100 Continue\r\n\r\n";
        const std::string final = "HTTP/1.1 200 OK\r\n"
                                  "Content-Length: 2\r\n"
                                  "Connection: close\r\n\r\n"
                                  "OK";
        (void)client->sendAll(interim.data(), interim.size());
        std::this_thread::sleep_for(10ms);
        (void)client->sendAll(final.data(), final.size());
    });

    HttpClient::Options opts;
    opts.connectTimeout = Milliseconds{500};
    opts.requestTimeout = Milliseconds{500};
    auto result = HttpClient{opts}.get(
        "http://127.0.0.1:" + std::to_string(port.value()) + "/");

    serverThread.join();

    REQUIRE(result.isSuccess());
    if (!result.isSuccess()) return;
    REQUIRE(result.value().statusCode() == 200);
    REQUIRE(result.value().body() == "OK");
}

static void test_https_scheme_rejected() {
    BEGIN_TEST("HttpClient rejects direct https URLs with explicit error");
#ifndef AISOCKS_ENABLE_TLS
    HttpClient::Options opts;
    opts.connectTimeout = Milliseconds{150};
    opts.requestTimeout = Milliseconds{150};
    HttpClient client{opts};

    auto result = client.get("https://example.com/");
    REQUIRE(!result.isSuccess());
    REQUIRE(
        result.message().find("HTTPS is not supported") != std::string::npos);
#endif // !AISOCKS_ENABLE_TLS — HTTPS is supported in TLS builds; tested
       // separately
}

static void test_redirect_to_https_is_rejected() {
    BEGIN_TEST("HttpClient rejects redirects to https URLs");
#ifndef AISOCKS_ENABLE_TLS
    RedirectToHttpsServer server;
    std::thread serverThread(
        [&] { server.run(ClientLimit::Unlimited, Milliseconds{1}); });
    server.waitReady();

    HttpClient::Options opts;
    opts.connectTimeout = Milliseconds{500};
    opts.requestTimeout = Milliseconds{500};
    auto result = HttpClient{opts}.get("http://127.0.0.1:"
        + std::to_string(server.serverPort().value()) + "/");

    server.requestStop();
    serverThread.join();

    REQUIRE(!result.isSuccess());
    REQUIRE(
        result.message().find("HTTPS is not supported") != std::string::npos);
#endif // !AISOCKS_ENABLE_TLS — redirect-to-HTTPS is supported in TLS builds
}

static void test_invalid_authority_port_rejected() {
    BEGIN_TEST("HttpClient rejects invalid authority ports with trailing junk");

    HttpClient::Options opts;
    opts.connectTimeout = Milliseconds{200};
    opts.requestTimeout = Milliseconds{200};
    HttpClient client{opts};

    auto ipv4 = client.get("http://127.0.0.1:80abc/");
    REQUIRE(!ipv4.isSuccess());
    REQUIRE(ipv4.message().find("Invalid URL authority") != std::string::npos);

    auto ipv6 = client.get("http://[::1]:443abc/");
    REQUIRE(!ipv6.isSuccess());
    REQUIRE(ipv6.message().find("Invalid URL authority") != std::string::npos);
}

static void test_resolve_url_relative_redirects() {
    BEGIN_TEST("HttpClient::resolveUrl handles relative Location values");

    const std::string base = "http://example.com/a/b/c?old=1#frag";

    REQUIRE(HttpClient::resolveUrl(base, "https://other.test/x")
        == "https://other.test/x");
    REQUIRE(
        HttpClient::resolveUrl(base, "//cdn.test/r") == "http://cdn.test/r");
    REQUIRE(HttpClient::resolveUrl(base, "/root") == "http://example.com/root");
    REQUIRE(
        HttpClient::resolveUrl(base, "next") == "http://example.com/a/b/next");
    REQUIRE(HttpClient::resolveUrl(base, "../up") == "http://example.com/a/up");
    REQUIRE(HttpClient::resolveUrl(base, "./here?x=1")
        == "http://example.com/a/b/here?x=1");
    REQUIRE(
        HttpClient::resolveUrl(base, "dir/") == "http://example.com/a/b/dir/");
    REQUIRE(
        HttpClient::resolveUrl(base, "../up/") == "http://example.com/a/up/");
    REQUIRE(
        HttpClient::resolveUrl(base, "?q=2") == "http://example.com/a/b/c?q=2");
    REQUIRE(HttpClient::resolveUrl(base, "#new")
        == "http://example.com/a/b/c?old=1#new");
}

static void test_keepalive_connection_reuse() {
    BEGIN_TEST("HttpClient reuses keep-alive socket across sequential GETs");

    ReuseTrackingServer server;
    std::thread serverThread(
        [&] { server.run(ClientLimit::Unlimited, Milliseconds{1}); });
    server.waitReady();
    REQUIRE(server.isValid());
    if (!server.isValid()) {
        server.requestStop();
        serverThread.join();
        return;
    }

    HttpClient::Options opts;
    opts.connectTimeout = Milliseconds{500};
    opts.requestTimeout = Milliseconds{500};
    HttpClient client{opts};
    const std::string baseUrl
        = "http://127.0.0.1:" + std::to_string(server.serverPort().value());

    for (int i = 0; i < 3; ++i) {
        auto result = client.get(baseUrl + "/reuse");
        REQUIRE(result.isSuccess());
        if (!result.isSuccess()) {
            server.requestStop();
            serverThread.join();
            return;
        }
        REQUIRE(result.value().statusCode() == 200);
        REQUIRE(result.value().body() == "OK");
    }

    server.requestStop();
    serverThread.join();

    REQUIRE(server.requestsServed.load() >= 3);
    REQUIRE(server.connectionsAccepted.load() == 1);
}

static void test_connection_close_header_disables_reuse() {
    BEGIN_TEST("HttpClient Connection: close header disables socket reuse");

    ReuseTrackingServer server;
    std::thread serverThread(
        [&] { server.run(ClientLimit::Unlimited, Milliseconds{1}); });
    server.waitReady();
    REQUIRE(server.isValid());
    if (!server.isValid()) {
        server.requestStop();
        serverThread.join();
        return;
    }

    HttpClient::Options opts;
    opts.connectTimeout = Milliseconds{500};
    opts.requestTimeout = Milliseconds{500};
    opts.setHeader("Connection", "close");
    HttpClient client{opts};
    const std::string baseUrl
        = "http://127.0.0.1:" + std::to_string(server.serverPort().value());

    for (int i = 0; i < 2; ++i) {
        auto result = client.get(baseUrl + "/close");
        REQUIRE(result.isSuccess());
        if (!result.isSuccess()) {
            server.requestStop();
            serverThread.join();
            return;
        }
        REQUIRE(result.value().statusCode() == 200);
    }

    server.requestStop();
    serverThread.join();

    REQUIRE(server.connectionsAccepted.load() >= 2);
}

// ---------------------------------------------------------------------------

int main() {
    printf("=== HttpClient + HttpPollServer integration tests ===\n");

    {
        ServerGuard sg;
        run_happy_path_tests(sg);
    }
    test_invalid_host_returns_failure();
    test_bad_dns_fails();
    test_connect_timeout_fails();
    test_request_timeout_fails();
    test_request_timeout_is_total_deadline();
    test_server_init_failure_unblocks_waiter();
    test_interim_100_response_is_ignored();
    test_keepalive_connection_reuse();
    test_connection_close_header_disables_reuse();
    test_https_scheme_rejected();
    test_redirect_to_https_is_rejected();
    test_invalid_authority_port_rejected();
    test_resolve_url_relative_redirects();

    return test_summary();
}
