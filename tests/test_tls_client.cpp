// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// TLS integration tests: HttpClient (HTTPS) talking to a self-signed
// HttpsPollServer in-process.
//
// These tests require AISOCKS_ENABLE_TLS=ON and the test certificate pair
// generated at tests/certs/test_cert.pem + tests/certs/test_key.pem.
//
// Server architecture:
//   TestHttpsServer derives from HttpPollServer and overrides the four TLS
//   virtual hooks (isTlsMode, onTlsClientConnected, doTlsHandshakeStep,
//   tlsRead, tlsWrite) to add per-connection OpenSSL sessions without
//   touching the poller or framing layers.

#ifdef AISOCKS_ENABLE_TLS

#include "HttpClient.h"
#include "HttpPollServer.h"
#include "TlsOpenSsl.h"
#include "test_helpers.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <openssl/ssl.h>
#include <string>
#include <thread>

using namespace aiSocks;

// ---------------------------------------------------------------------------
// Derive the source tree root from __FILE__ so cert paths are absolute and
// work regardless of which directory CTest runs the binary from.
// ---------------------------------------------------------------------------
static std::string sourceRoot() {
    std::string path = __FILE__;
    const std::string marker = "/tests/test_tls_client.cpp";
    const size_t pos = path.rfind(marker);
    if (pos != std::string::npos) path.resize(pos);
    return path;
}

// ---------------------------------------------------------------------------
// TestHttpsServer — HttpPollServer with TLS hooks wired to OpenSSL sessions.
// ---------------------------------------------------------------------------
class TestHttpsServer : public HttpPollServer {
    public:
    explicit TestHttpsServer(const std::string& certPath,
        const std::string& keyPath, const std::string& bindHost = "127.0.0.1",
        AddressFamily family = AddressFamily::IPv4)
        : HttpPollServer(ServerBind{bindHost, Port{0}}, family) {
        setHandleSignals(false);

        std::string err;
        ctx_ = TlsContext::create(TlsContext::Mode::Server, &err);
        if (!ctx_) return;
        if (!ctx_->loadCertificateChain(certPath, keyPath, &err)) {
            ctx_.reset();
        }
    }

    bool tlsReady() const noexcept { return ctx_ != nullptr; }

    std::string lastSniName() const {
        std::lock_guard<std::mutex> lk(sniMtx_);
        return lastSniName_;
    }

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
        s.responseBuf
            = makeResponse("HTTP/1.1 200 OK", "text/plain", "Hello, HTTPS!");
        s.responseView = s.responseBuf;
        requestsServed.fetch_add(1, std::memory_order_relaxed);
    }

    // ---- TLS hooks -------------------------------------------------------

    bool isTlsMode(const HttpClientState& /*s*/) const override {
        return ctx_ != nullptr;
    }

    void onTlsClientConnected(TcpSocket& sock, HttpClientState& s) override {
        if (!ctx_) return;
        std::string err;
        auto sess = TlsSession::create(ctx_->nativeHandle(), &err);
        if (!sess) return;
        if (!sess->attachSocket(static_cast<int>(sock.getNativeHandle()), &err))
            return;
        sess->setAcceptState();
        s.tlsSession = std::move(sess);
        s.tlsHandshakeDone = false;
        s.tlsWantsWrite = false;
    }

    ServerResult doTlsHandshakeStep(
        TcpSocket& /*sock*/, HttpClientState& s) override {
        if (!s.tlsSession) return ServerResult::Disconnect;
        const int r = s.tlsSession->handshake();
        if (r == 1) {
            const char* sni = SSL_get_servername(
                s.tlsSession->nativeHandle(), TLSEXT_NAMETYPE_host_name);
            {
                std::lock_guard<std::mutex> lk(sniMtx_);
                lastSniName_ = sni ? std::string(sni) : std::string{};
            }
            s.tlsHandshakeDone = true;
            s.tlsWantsWrite = false;
            return ServerResult::KeepConnection;
        }
        const int e = s.tlsSession->getLastErrorCode(r);
        if (e == SSL_ERROR_WANT_READ) {
            s.tlsWantsWrite = false;
            return ServerResult::KeepConnection;
        }
        if (e == SSL_ERROR_WANT_WRITE) {
            s.tlsWantsWrite = true;
            return ServerResult::KeepConnection;
        }
        return ServerResult::Disconnect;
    }

    int tlsRead(TcpSocket& /*sock*/, HttpClientState& s, void* buf,
        size_t len) override {
        if (!s.tlsSession) return -1;
        return s.tlsSession->read(buf, static_cast<int>(len));
    }

    int tlsWrite(TcpSocket& /*sock*/, HttpClientState& s, const char* data,
        size_t len) override {
        if (!s.tlsSession) return 0;
        size_t done = 0;
        while (done < len) {
            int n = s.tlsSession->write(
                data + done, static_cast<int>(len - done));
            if (n > 0) {
                done += static_cast<size_t>(n);
                continue;
            }
            const int e = s.tlsSession->getLastErrorCode(n);
            if (e == SSL_ERROR_WANT_WRITE || e == SSL_ERROR_WANT_READ) continue;
            break;
        }
        return static_cast<int>(done);
    }

    private:
    std::unique_ptr<TlsContext> ctx_;
    std::atomic<bool> ready_{false};
    std::mutex readyMtx_;
    std::condition_variable readyCv_;
    mutable std::mutex sniMtx_;
    std::string lastSniName_;
};

static bool hasIpv6Loopback_() {
    auto srv = TcpSocket::createRaw(AddressFamily::IPv6);
    if (!srv.setReuseAddress(true)) return false;
    if (!srv.bind("::1", Port{0}) || !srv.listen(1)) return false;
    return true;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void test_https_client_basic_get() {
    BEGIN_TEST("HttpClient HTTPS GET returns 200 OK with correct body");

    const std::string root = sourceRoot();
    const std::string cert = root + "/tests/certs/test_cert.pem";
    const std::string key = root + "/tests/certs/test_key.pem";

    TestHttpsServer server{cert, key};
    REQUIRE(server.tlsReady());

    std::thread serverThread(
        [&] { server.run(ClientLimit::Unlimited, Milliseconds{5}); });
    server.waitReady();

    HttpClient::Options opts;
    opts.connectTimeout = Milliseconds{2000};
    opts.requestTimeout = Milliseconds{2000};
    opts.verifyCertificate = false; // self-signed cert in test
    HttpClient client{opts};

    const std::string url = "https://127.0.0.1:"
        + std::to_string(server.serverPort().value()) + "/hello";
    auto result = client.get(url);

    server.requestStop();
    serverThread.join();

    REQUIRE(result.isSuccess());
    REQUIRE(result.value().statusCode() == 200);
    REQUIRE(
        result.value().body().find("Hello, HTTPS!") != std::string_view::npos);
    REQUIRE(server.requestsServed.load() == 1);
}

static void test_https_client_multiple_requests_keep_alive() {
    BEGIN_TEST(
        "HttpClient HTTPS keep-alive reuses TLS session across requests");

    const std::string root = sourceRoot();
    const std::string cert = root + "/tests/certs/test_cert.pem";
    const std::string key = root + "/tests/certs/test_key.pem";

    TestHttpsServer server{cert, key};
    REQUIRE(server.tlsReady());

    std::thread serverThread(
        [&] { server.run(ClientLimit::Unlimited, Milliseconds{5}); });
    server.waitReady();

    HttpClient::Options opts;
    opts.connectTimeout = Milliseconds{2000};
    opts.requestTimeout = Milliseconds{2000};
    opts.verifyCertificate = false;
    HttpClient client{opts};

    const std::string base
        = "https://127.0.0.1:" + std::to_string(server.serverPort().value());

    auto r1 = client.get(base + "/first");
    auto r2 = client.get(base + "/second");
    auto r3 = client.get(base + "/third");

    server.requestStop();
    serverThread.join();

    REQUIRE(r1.isSuccess());
    REQUIRE(r2.isSuccess());
    REQUIRE(r3.isSuccess());
    REQUIRE(server.requestsServed.load() == 3);
}

static void test_https_verify_enabled_trusted_ca_and_matching_host() {
    BEGIN_TEST("HttpClient HTTPS verify enabled succeeds with trusted cert and "
               "matching host");

    const std::string root = sourceRoot();
    const std::string cert = root + "/tests/certs/test_cert.pem";
    const std::string key = root + "/tests/certs/test_key.pem";

    TestHttpsServer server{cert, key};
    REQUIRE(server.tlsReady());

    std::thread serverThread(
        [&] { server.run(ClientLimit::Unlimited, Milliseconds{5}); });
    server.waitReady();

    HttpClient::Options opts;
    opts.connectTimeout = Milliseconds{2000};
    opts.requestTimeout = Milliseconds{2000};
    opts.verifyCertificate = true;
    opts.caCertFile = cert;
    HttpClient client{opts};

    const std::string url = "https://127.0.0.1:"
        + std::to_string(server.serverPort().value()) + "/verified";
    auto result = client.get(url);

    server.requestStop();
    serverThread.join();

    if (!result.isSuccess()) {
        REQUIRE_MSG(false,
            ("IPv6 verified request failed: " + result.message()).c_str());
        return;
    }
    REQUIRE(result.value().statusCode() == 200);
}

static void test_https_verify_enabled_fails_on_wrong_hostname() {
    BEGIN_TEST("HttpClient HTTPS verify enabled fails for hostname mismatch");

    const std::string root = sourceRoot();
    const std::string cert = root + "/tests/certs/test_cert.pem";
    const std::string key = root + "/tests/certs/test_key.pem";

    TestHttpsServer server{cert, key};
    REQUIRE(server.tlsReady());

    std::thread serverThread(
        [&] { server.run(ClientLimit::Unlimited, Milliseconds{5}); });
    server.waitReady();

    HttpClient::Options opts;
    opts.connectTimeout = Milliseconds{2000};
    opts.requestTimeout = Milliseconds{2000};
    opts.verifyCertificate = true;
    opts.caCertFile = cert;
    HttpClient client{opts};

    const std::string url = "https://localhost:"
        + std::to_string(server.serverPort().value()) + "/wrong-host";
    auto result = client.get(url);

    server.requestStop();
    serverThread.join();

    REQUIRE(!result.isSuccess());
    REQUIRE(!result.message().empty());
}

static void test_https_verify_enabled_fails_for_untrusted_self_signed() {
    BEGIN_TEST(
        "HttpClient HTTPS verify enabled fails for untrusted self-signed cert");

    const std::string root = sourceRoot();
    const std::string cert = root + "/tests/certs/test_cert.pem";
    const std::string key = root + "/tests/certs/test_key.pem";

    TestHttpsServer server{cert, key};
    REQUIRE(server.tlsReady());

    std::thread serverThread(
        [&] { server.run(ClientLimit::Unlimited, Milliseconds{5}); });
    server.waitReady();

    HttpClient::Options opts;
    opts.connectTimeout = Milliseconds{2000};
    opts.requestTimeout = Milliseconds{2000};
    opts.verifyCertificate = true;
    HttpClient client{opts};

    const std::string url = "https://127.0.0.1:"
        + std::to_string(server.serverPort().value()) + "/untrusted";
    auto result = client.get(url);

    server.requestStop();
    serverThread.join();

    REQUIRE(!result.isSuccess());
    REQUIRE(result.message().find("TLS setup") != std::string::npos);
}

static void test_https_verify_enabled_invalid_ca_file_fails_setup() {
    BEGIN_TEST(
        "HttpClient HTTPS verify enabled fails setup for invalid CA file");

    const std::string root = sourceRoot();
    const std::string cert = root + "/tests/certs/test_cert.pem";
    const std::string key = root + "/tests/certs/test_key.pem";

    TestHttpsServer server{cert, key};
    REQUIRE(server.tlsReady());

    std::thread serverThread(
        [&] { server.run(ClientLimit::Unlimited, Milliseconds{5}); });
    server.waitReady();

    HttpClient::Options opts;
    opts.connectTimeout = Milliseconds{2000};
    opts.requestTimeout = Milliseconds{2000};
    opts.verifyCertificate = true;
    opts.caCertFile = "/nonexistent/ca-bundle.pem";
    HttpClient client{opts};

    const std::string url = "https://127.0.0.1:"
        + std::to_string(server.serverPort().value()) + "/invalid-ca-file";
    auto result = client.get(url);

    server.requestStop();
    serverThread.join();

    REQUIRE(!result.isSuccess());
    REQUIRE(result.message().find("TLS setup") != std::string::npos);
}

static void test_https_verify_enabled_invalid_ca_dir_fails_setup() {
    BEGIN_TEST(
        "HttpClient HTTPS verify enabled fails setup for invalid CA dir");

    const std::string root = sourceRoot();
    const std::string cert = root + "/tests/certs/test_cert.pem";
    const std::string key = root + "/tests/certs/test_key.pem";

    TestHttpsServer server{cert, key};
    REQUIRE(server.tlsReady());

    std::thread serverThread(
        [&] { server.run(ClientLimit::Unlimited, Milliseconds{5}); });
    server.waitReady();

    HttpClient::Options opts;
    opts.connectTimeout = Milliseconds{2000};
    opts.requestTimeout = Milliseconds{2000};
    opts.verifyCertificate = true;
    opts.caCertDir = "/nonexistent/ca-dir";
    HttpClient client{opts};

    const std::string url = "https://127.0.0.1:"
        + std::to_string(server.serverPort().value()) + "/invalid-ca-dir";
    auto result = client.get(url);

    server.requestStop();
    serverThread.join();

    REQUIRE(!result.isSuccess());
    REQUIRE(result.message().find("TLS setup") != std::string::npos);
}

static void test_https_verify_enabled_ca_file_plus_invalid_dir_fails_setup() {
    BEGIN_TEST("HttpClient HTTPS verify enabled with CA file plus invalid CA "
               "dir fails setup deterministically");

    const std::string root = sourceRoot();
    const std::string cert = root + "/tests/certs/test_cert.pem";
    const std::string key = root + "/tests/certs/test_key.pem";

    TestHttpsServer server{cert, key};
    REQUIRE(server.tlsReady());

    std::thread serverThread(
        [&] { server.run(ClientLimit::Unlimited, Milliseconds{5}); });
    server.waitReady();

    HttpClient::Options opts;
    opts.connectTimeout = Milliseconds{2000};
    opts.requestTimeout = Milliseconds{2000};
    opts.verifyCertificate = true;
    opts.caCertFile = cert;
    opts.caCertDir = "/nonexistent/ca-dir";
    HttpClient client{opts};

    const std::string url = "https://127.0.0.1:"
        + std::to_string(server.serverPort().value()) + "/ca-file-plus-dir";
    auto result = client.get(url);

    server.requestStop();
    serverThread.join();

    REQUIRE(!result.isSuccess());
    REQUIRE(result.message().find("TLS setup") != std::string::npos);
}

static void test_https_set_options_rebuilds_tls_context() {
    BEGIN_TEST("HttpClient setOptions rebuilds TLS context with new trust "
               "settings");

    const std::string root = sourceRoot();
    const std::string cert = root + "/tests/certs/test_cert.pem";
    const std::string key = root + "/tests/certs/test_key.pem";

    TestHttpsServer server{cert, key};
    REQUIRE(server.tlsReady());

    std::thread serverThread(
        [&] { server.run(ClientLimit::Unlimited, Milliseconds{5}); });
    server.waitReady();

    HttpClient::Options opts;
    opts.connectTimeout = Milliseconds{2000};
    opts.requestTimeout = Milliseconds{2000};
    opts.verifyCertificate = true;
    opts.caCertFile = cert;
    HttpClient client{opts};

    const std::string url = "https://127.0.0.1:"
        + std::to_string(server.serverPort().value()) + "/set-options";
    auto first = client.get(url);
    REQUIRE(first.isSuccess());

    HttpClient::Options invalid = opts;
    invalid.caCertFile = "/nonexistent/ca-bundle.pem";
    client.setOptions(invalid);

    auto second = client.get(url);

    server.requestStop();
    serverThread.join();

    REQUIRE(!second.isSuccess());
    REQUIRE(second.message().find("TLS setup") != std::string::npos);
}

static void test_https_ip_literal_does_not_send_sni() {
    BEGIN_TEST("HttpClient HTTPS with IP literal does not send SNI");

    const std::string root = sourceRoot();
    const std::string cert = root + "/tests/certs/test_cert.pem";
    const std::string key = root + "/tests/certs/test_key.pem";

    TestHttpsServer server{cert, key};
    REQUIRE(server.tlsReady());

    std::thread serverThread(
        [&] { server.run(ClientLimit::Unlimited, Milliseconds{5}); });
    server.waitReady();

    HttpClient::Options opts;
    opts.connectTimeout = Milliseconds{2000};
    opts.requestTimeout = Milliseconds{2000};
    opts.verifyCertificate = false;
    HttpClient client{opts};

    const std::string url = "https://127.0.0.1:"
        + std::to_string(server.serverPort().value()) + "/no-sni-ip";
    auto result = client.get(url);

    server.requestStop();
    serverThread.join();

    REQUIRE(result.isSuccess());
    REQUIRE(server.lastSniName().empty());
}

static void test_https_dns_host_sends_sni() {
    BEGIN_TEST("HttpClient HTTPS with DNS host sends SNI");

    const std::string root = sourceRoot();
    const std::string cert = root + "/tests/certs/test_cert.pem";
    const std::string key = root + "/tests/certs/test_key.pem";

    TestHttpsServer server{cert, key};
    REQUIRE(server.tlsReady());

    std::thread serverThread(
        [&] { server.run(ClientLimit::Unlimited, Milliseconds{5}); });
    server.waitReady();

    HttpClient::Options opts;
    opts.connectTimeout = Milliseconds{2000};
    opts.requestTimeout = Milliseconds{2000};
    opts.verifyCertificate = false;
    HttpClient client{opts};

    const std::string url = "https://localhost:"
        + std::to_string(server.serverPort().value()) + "/sni-dns";
    auto result = client.get(url);

    server.requestStop();
    serverThread.join();

    REQUIRE(result.isSuccess());
    REQUIRE(server.lastSniName() == "localhost");
}

static void test_https_verify_enabled_ipv6_san_match_succeeds() {
    BEGIN_TEST("HttpClient HTTPS verify enabled succeeds for IPv6 SAN match");

    if (!hasIpv6Loopback_()) {
        REQUIRE_MSG(true, "SKIP - IPv6 loopback not available on this system");
        return;
    }

    const std::string root = sourceRoot();
    const std::string cert = root + "/tests/certs/test_cert_ipv6.pem";
    const std::string key = root + "/tests/certs/test_key_ipv6.pem";

    TestHttpsServer server{cert, key, "::1", AddressFamily::IPv6};
    REQUIRE(server.tlsReady());

    std::thread serverThread(
        [&] { server.run(ClientLimit::Unlimited, Milliseconds{5}); });
    server.waitReady();

    HttpClient::Options opts;
    opts.connectTimeout = Milliseconds{2000};
    opts.requestTimeout = Milliseconds{2000};
    opts.verifyCertificate = true;
    opts.caCertFile = cert;
    HttpClient client{opts};

    const std::string url = "https://[::1]:"
        + std::to_string(server.serverPort().value()) + "/ipv6-verified";
    auto result = client.get(url);

    server.requestStop();
    serverThread.join();

    REQUIRE(result.isSuccess());
    REQUIRE(result.value().statusCode() == 200);
}

static void test_https_verify_enabled_ipv6_san_mismatch_fails() {
    BEGIN_TEST("HttpClient HTTPS verify enabled fails for IPv6 SAN mismatch");

    if (!hasIpv6Loopback_()) {
        REQUIRE_MSG(true, "SKIP - IPv6 loopback not available on this system");
        return;
    }

    const std::string root = sourceRoot();
    const std::string cert = root + "/tests/certs/test_cert_ipv6_mismatch.pem";
    const std::string key = root + "/tests/certs/test_key_ipv6_mismatch.pem";

    TestHttpsServer server{cert, key, "::1", AddressFamily::IPv6};
    REQUIRE(server.tlsReady());

    std::thread serverThread(
        [&] { server.run(ClientLimit::Unlimited, Milliseconds{5}); });
    server.waitReady();

    HttpClient::Options opts;
    opts.connectTimeout = Milliseconds{2000};
    opts.requestTimeout = Milliseconds{2000};
    opts.verifyCertificate = true;
    opts.caCertFile = cert;
    HttpClient client{opts};

    const std::string url = "https://[::1]:"
        + std::to_string(server.serverPort().value()) + "/ipv6-mismatch";
    auto result = client.get(url);

    server.requestStop();
    serverThread.join();

    REQUIRE(!result.isSuccess());
    REQUIRE(result.message().find("TLS setup") != std::string::npos);
}

static void test_https_verify_enabled_rejects_non_ascii_dns_host() {
    BEGIN_TEST("HttpClient HTTPS verify enabled rejects non-ASCII DNS host; "
               "requires punycode");

    HttpClient::Options opts;
    opts.connectTimeout = Milliseconds{2000};
    opts.requestTimeout = Milliseconds{2000};
    opts.verifyCertificate = true;
    HttpClient client{opts};

    const std::string host = "t\xC3\xA9st.invalid";
    const std::string url = "https://" + host + "/idn";
    auto result = client.get(url);

    REQUIRE(!result.isSuccess());
    REQUIRE(result.message().find("punycode") != std::string::npos);
}

static void test_https_cert_load_failure() {
    BEGIN_TEST("TestHttpsServer reports failure when cert files are missing");

    TestHttpsServer server{"/nonexistent/cert.pem", "/nonexistent/key.pem"};
    REQUIRE(!server.tlsReady());
}

static void test_https_wrong_port_fails() {
    BEGIN_TEST("HttpClient HTTPS to refused port returns connection failure");

    HttpClient::Options opts;
    opts.connectTimeout = Milliseconds{200};
    opts.requestTimeout = Milliseconds{200};
    opts.verifyCertificate = false;
    HttpClient client{opts};

    // Port 1 is almost certainly refused on any machine.
    auto result = client.get("https://127.0.0.1:1/");
    REQUIRE(!result.isSuccess());
    REQUIRE(
        result.message().find("HTTPS is not supported") == std::string::npos);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    test_https_cert_load_failure();
    test_https_wrong_port_fails();
    test_https_client_basic_get();
    test_https_client_multiple_requests_keep_alive();
    test_https_verify_enabled_trusted_ca_and_matching_host();
    test_https_verify_enabled_fails_on_wrong_hostname();
    test_https_verify_enabled_fails_for_untrusted_self_signed();
    test_https_verify_enabled_invalid_ca_file_fails_setup();
    test_https_verify_enabled_invalid_ca_dir_fails_setup();
    test_https_verify_enabled_ca_file_plus_invalid_dir_fails_setup();
    test_https_set_options_rebuilds_tls_context();
    test_https_ip_literal_does_not_send_sni();
    test_https_dns_host_sends_sni();
    test_https_verify_enabled_ipv6_san_match_succeeds();
    test_https_verify_enabled_ipv6_san_mismatch_fails();
    test_https_verify_enabled_rejects_non_ascii_dns_host();

    return test_summary();
}

#else // !AISOCKS_ENABLE_TLS

int main() {
    return 0; /* nothing to test without TLS */
}

#endif // AISOCKS_ENABLE_TLS
