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
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace aiSocks;

static bool tlsDebugEnabled_() {
    const char* envName = "AISOCKS_TLS_DEBUG";
#ifdef _MSC_VER
    char* value = nullptr;
    size_t len = 0;
    if (_dupenv_s(&value, &len, envName) != 0 || value == nullptr) {
        return false;
    }
    const bool enabled = value[0] != '\0' && value[0] != '0';
    std::free(value);
    return enabled;
#else
    const char* value = std::getenv(envName);
    return value != nullptr && value[0] != '\0' && value[0] != '0';
#endif
}

static void tlsDebugLog_(const std::string& msg) {
    if (!tlsDebugEnabled_()) return;
    std::fprintf(stderr, "[tls-debug] %s\n", msg.c_str());
}

// ---------------------------------------------------------------------------
// Derive the source tree root from __FILE__ so cert paths are absolute and
// work regardless of which directory CTest runs the binary from.
// ---------------------------------------------------------------------------
static std::string sourceRoot() {
    std::string path = std::filesystem::path(__FILE__).generic_string();
    const std::string marker = "/tests/test_tls_client.cpp";
    const size_t pos = path.rfind(marker);
    if (pos != std::string::npos) {
        const std::string root = path.substr(0, pos);
        tlsDebugLog_("__FILE__=" + path);
        tlsDebugLog_("sourceRoot=" + root);
        return root;
    }
    tlsDebugLog_(
        "__FILE__ marker not found, falling back to cwd. __FILE__=" + path);
    return ".";
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
        if (!ctx_) {
            tlsInitError_ = err.empty() ? "TlsContext::create failed" : err;
            tlsDebugLog_("TlsContext::create failed: " + tlsInitError_);
            return;
        }
        if (!ctx_->loadCertificateChain(certPath, keyPath, &err)) {
            tlsInitError_ = err.empty() ? "loadCertificateChain failed" : err;
            tlsDebugLog_("loadCertificateChain failed: cert=" + certPath
                + " key=" + keyPath + " error=" + tlsInitError_);
            ctx_.reset();
            return;
        }

        tlsDebugLog_("TLS server context initialized: cert=" + certPath
            + " key=" + keyPath);
    }

    bool tlsReady() const noexcept { return ctx_ != nullptr; }
    std::string tlsInitError() const { return tlsInitError_; }

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
    std::string tlsInitError_;
};

class RedirectHttpsServer : public TestHttpsServer {
    public:
    RedirectHttpsServer(const std::string& certPath, const std::string& keyPath,
        std::string location, const std::string& bindHost = "127.0.0.1",
        AddressFamily family = AddressFamily::IPv4)
        : TestHttpsServer(certPath, keyPath, bindHost, family)
        , location_(std::move(location)) {}

    protected:
    void buildResponse(HttpClientState& s) override {
        s.responseBuf = "HTTP/1.1 302 Found\r\n"
                        "Location: "
            + location_
            + "\r\n"
              "Content-Length: 0\r\n"
              "Connection: close\r\n\r\n";
        s.responseView = s.responseBuf;
        requestsServed.fetch_add(1, std::memory_order_relaxed);
    }

    private:
    std::string location_;
};

class TestHttpServer : public HttpPollServer {
    public:
    explicit TestHttpServer(const std::string& body = "Hello, HTTP!")
        : HttpPollServer(ServerBind{"127.0.0.1", Port{0}}), body_(body) {
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
        s.responseBuf = makeResponse("HTTP/1.1 200 OK", "text/plain", body_);
        s.responseView = s.responseBuf;
    }

    private:
    std::string body_;
    std::atomic<bool> ready_{false};
    std::mutex readyMtx_;
    std::condition_variable readyCv_;
};

static bool hasIpv6Loopback_() {
    auto srv = TcpSocket::createRaw(AddressFamily::IPv6);
    if (!srv.setReuseAddress(true)) return false;
    if (!srv.bind("::1", Port{0}) || !srv.listen(1)) return false;
    return true;
}

struct ScopedTempDir {
    std::filesystem::path path;
    ~ScopedTempDir() {
        if (path.empty()) return;
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

static bool copyFile_(const std::string& src, const std::string& dst) {
    std::ifstream in(src, std::ios::binary);
    if (!in) return false;
    std::ofstream out(dst, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << in.rdbuf();
    return static_cast<bool>(out);
}

static FILE* openFileReadBinary_(const std::string& path) {
#ifdef _MSC_VER
    FILE* fp = nullptr;
    if (fopen_s(&fp, path.c_str(), "rb") != 0) return nullptr;
    return fp;
#else
    return std::fopen(path.c_str(), "rb");
#endif
}

static std::string getEnvValue_(const char* name) {
#ifdef _MSC_VER
    char* value = nullptr;
    size_t len = 0;
    if (_dupenv_s(&value, &len, name) != 0 || !value) return {};
    std::string out(value);
    std::free(value);
    return out;
#else
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string{};
#endif
}

static bool appendHashedCaCert_(const std::string& certPemPath,
    const std::filesystem::path& capathDir, std::string& err) {
    FILE* fp = openFileReadBinary_(certPemPath);
    if (!fp) {
        err = "failed to open cert PEM for capath fixture";
        return false;
    }

    X509* cert = PEM_read_X509(fp, nullptr, nullptr, nullptr);
    std::fclose(fp);
    if (!cert) {
        err = "failed to parse cert PEM for capath fixture";
        return false;
    }

    const unsigned long hash = X509_subject_name_hash(cert);
    X509_free(cert);

    for (int suffix = 0; suffix < 16; ++suffix) {
        char hashFile[32] = {};
        std::snprintf(hashFile, sizeof(hashFile), "%08lx.%d", hash, suffix);
        const std::filesystem::path hashedCert = capathDir / hashFile;
        if (std::filesystem::exists(hashedCert)) continue;
        if (!copyFile_(certPemPath, hashedCert.string())) {
            err = "failed to write hashed cert file in capath fixture";
            return false;
        }
        return true;
    }

    err = "no free hash suffix available in capath fixture";
    return false;
}

static bool createHashedCaDirFixture_(
    const std::string& certPemPath, ScopedTempDir& out, std::string& err) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    std::ostringstream dirName;
    dirName << "aisocks-capath-" << now.count();
    out.path = std::filesystem::temp_directory_path() / dirName.str();

    std::error_code ec;
    if (!std::filesystem::create_directories(out.path, ec)) {
        err = "failed to create capath fixture directory";
        return false;
    }

    return appendHashedCaCert_(certPemPath, out.path, err);
}

static bool createHashedCaDirFixtureForCerts_(
    const std::vector<std::string>& certPemPaths, ScopedTempDir& out,
    std::string& err) {
    if (certPemPaths.empty()) {
        err = "no cert paths provided for capath fixture";
        return false;
    }

    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    std::ostringstream dirName;
    dirName << "aisocks-capath-multi-" << now.count();
    out.path = std::filesystem::temp_directory_path() / dirName.str();

    std::error_code ec;
    if (!std::filesystem::create_directories(out.path, ec)) {
        err = "failed to create capath fixture directory";
        return false;
    }

    for (const auto& certPemPath : certPemPaths) {
        if (!appendHashedCaCert_(certPemPath, out.path, err)) return false;
    }

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

    tlsDebugLog_("basic_get cert path=" + cert);
    tlsDebugLog_("basic_get key path=" + key);

    TestHttpsServer server{cert, key};
    if (!server.tlsReady()) {
        tlsDebugLog_("basic_get tlsReady=false error=" + server.tlsInitError());
    }
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

    REQUIRE_MSG(result.isSuccess(),
        ("HTTPS basic GET failed: " + result.message()).c_str());
    if (!result.isSuccess()) return;
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

static void test_https_cache_is_not_reused_for_http_same_host_port() {
    BEGIN_TEST("HttpClient does not reuse HTTPS cached connection for HTTP on "
               "same host and port");

    const std::string root = sourceRoot();
    const std::string cert = root + "/tests/certs/test_cert.pem";
    const std::string key = root + "/tests/certs/test_key.pem";

    TestHttpsServer server{cert, key};
    REQUIRE(server.tlsReady());

    std::thread serverThread(
        [&] { server.run(ClientLimit::Unlimited, Milliseconds{5}); });
    server.waitReady();

    HttpClient::Options opts;
    opts.connectTimeout = Milliseconds{1200};
    opts.requestTimeout = Milliseconds{1200};
    opts.verifyCertificate = false;
    HttpClient client{opts};

    const std::string hostPort
        = "127.0.0.1:" + std::to_string(server.serverPort().value());

    auto httpsResult = client.get("https://" + hostPort + "/first");
    REQUIRE(httpsResult.isSuccess());
    if (!httpsResult.isSuccess()) {
        server.requestStop();
        serverThread.join();
        return;
    }

    auto httpResult = client.get("http://" + hostPort + "/plain-after-https");

    server.requestStop();
    serverThread.join();

    // If HTTP reused the cached HTTPS socket/session, this would incorrectly
    // succeed against the TLS server. Correct behavior is a failed plain-HTTP
    // request to a TLS-only endpoint.
    REQUIRE(!httpResult.isSuccess());
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

static void test_https_verify_enabled_ca_dir_only_succeeds() {
    BEGIN_TEST("HttpClient HTTPS verify enabled succeeds with CA dir only");

    const std::string root = sourceRoot();
    const std::string cert = root + "/tests/certs/test_cert.pem";
    const std::string key = root + "/tests/certs/test_key.pem";

    ScopedTempDir capathFixture;
    std::string fixtureErr;
    REQUIRE(createHashedCaDirFixture_(cert, capathFixture, fixtureErr));
    if (!fixtureErr.empty()) {
        REQUIRE_MSG(false, fixtureErr.c_str());
        return;
    }

    TestHttpsServer server{cert, key};
    REQUIRE(server.tlsReady());

    std::thread serverThread(
        [&] { server.run(ClientLimit::Unlimited, Milliseconds{5}); });
    server.waitReady();

    HttpClient::Options opts;
    opts.connectTimeout = Milliseconds{2000};
    opts.requestTimeout = Milliseconds{2000};
    opts.verifyCertificate = true;
    opts.caCertDir = capathFixture.path.string();
    HttpClient client{opts};

    const std::string url = "https://127.0.0.1:"
        + std::to_string(server.serverPort().value()) + "/ca-dir-only";
    auto result = client.get(url);

    server.requestStop();
    serverThread.join();

    REQUIRE_MSG(result.isSuccess(),
        ("CA dir-only request failed: " + result.message()).c_str());
    if (!result.isSuccess()) return;
    REQUIRE(result.value().statusCode() == 200);
}

static void test_https_verify_enabled_ca_file_plus_valid_dir_succeeds() {
    BEGIN_TEST("HttpClient HTTPS verify enabled succeeds with CA file plus "
               "valid CA dir");

    const std::string root = sourceRoot();
    const std::string cert = root + "/tests/certs/test_cert.pem";
    const std::string key = root + "/tests/certs/test_key.pem";

    ScopedTempDir capathFixture;
    std::string fixtureErr;
    REQUIRE(createHashedCaDirFixture_(cert, capathFixture, fixtureErr));
    if (!fixtureErr.empty()) {
        REQUIRE_MSG(false, fixtureErr.c_str());
        return;
    }

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
    opts.caCertDir = capathFixture.path.string();
    HttpClient client{opts};

    const std::string url = "https://127.0.0.1:"
        + std::to_string(server.serverPort().value()) + "/ca-file-plus-dir";
    auto result = client.get(url);

    server.requestStop();
    serverThread.join();

    REQUIRE_MSG(result.isSuccess(),
        ("CA file+dir request failed: " + result.message()).c_str());
    if (!result.isSuccess()) return;
    REQUIRE(result.value().statusCode() == 200);
}

static void test_https_redirect_to_different_host_with_verify_enabled() {
    BEGIN_TEST("HttpClient follows HTTPS redirect to different host with "
               "verify enabled");

    if (!hasIpv6Loopback_()) {
        REQUIRE_MSG(true, "SKIP - IPv6 loopback not available on this system");
        return;
    }

    const std::string root = sourceRoot();
    const std::string certV4 = root + "/tests/certs/test_cert.pem";
    const std::string keyV4 = root + "/tests/certs/test_key.pem";
    const std::string certV6 = root + "/tests/certs/test_cert_ipv6.pem";
    const std::string keyV6 = root + "/tests/certs/test_key_ipv6.pem";

    ScopedTempDir capathFixture;
    std::string fixtureErr;
    REQUIRE(createHashedCaDirFixtureForCerts_(
        {certV4, certV6}, capathFixture, fixtureErr));
    if (!fixtureErr.empty()) {
        REQUIRE_MSG(false, fixtureErr.c_str());
        return;
    }

    TestHttpsServer target{certV6, keyV6, "::1", AddressFamily::IPv6};
    REQUIRE(target.tlsReady());
    std::thread targetThread(
        [&] { target.run(ClientLimit::Unlimited, Milliseconds{5}); });
    target.waitReady();

    const std::string location = "https://[::1]:"
        + std::to_string(target.serverPort().value()) + "/redirected-v6";

    RedirectHttpsServer origin{certV4, keyV4, location};
    REQUIRE(origin.tlsReady());
    std::thread originThread(
        [&] { origin.run(ClientLimit::Unlimited, Milliseconds{5}); });
    origin.waitReady();

    HttpClient::Options opts;
    opts.connectTimeout = Milliseconds{2000};
    opts.requestTimeout = Milliseconds{2000};
    opts.verifyCertificate = true;
    opts.caCertDir = capathFixture.path.string();
    HttpClient client{opts};

    const std::string url = "https://127.0.0.1:"
        + std::to_string(origin.serverPort().value()) + "/start-redirect";
    auto result = client.get(url);

    origin.requestStop();
    target.requestStop();
    originThread.join();
    targetThread.join();

    REQUIRE(result.isSuccess());
    if (!result.isSuccess()) return;
    REQUIRE(result.value().statusCode() == 200);
    REQUIRE(result.value().finalUrl == location);
    REQUIRE(result.value().redirectChain.size() == 1);
    REQUIRE(result.value().redirectChain.front() == location);
}

static void test_https_to_http_redirect_behavior() {
    BEGIN_TEST("HttpClient follows HTTPS to HTTP redirect and reports final "
               "URL chain");

    const std::string root = sourceRoot();
    const std::string cert = root + "/tests/certs/test_cert.pem";
    const std::string key = root + "/tests/certs/test_key.pem";

    TestHttpServer httpTarget{"Hello, downgrade!"};
    std::thread httpThread(
        [&] { httpTarget.run(ClientLimit::Unlimited, Milliseconds{5}); });
    httpTarget.waitReady();

    const std::string location = "http://127.0.0.1:"
        + std::to_string(httpTarget.serverPort().value()) + "/downgraded";

    RedirectHttpsServer origin{cert, key, location};
    REQUIRE(origin.tlsReady());
    std::thread originThread(
        [&] { origin.run(ClientLimit::Unlimited, Milliseconds{5}); });
    origin.waitReady();

    HttpClient::Options opts;
    opts.connectTimeout = Milliseconds{2000};
    opts.requestTimeout = Milliseconds{2000};
    opts.verifyCertificate = true;
    opts.caCertFile = cert;
    HttpClient client{opts};

    const std::string url = "https://127.0.0.1:"
        + std::to_string(origin.serverPort().value()) + "/start-downgrade";
    auto result = client.get(url);

    origin.requestStop();
    httpTarget.requestStop();
    originThread.join();
    httpThread.join();

    REQUIRE(result.isSuccess());
    if (!result.isSuccess()) return;
    REQUIRE(result.value().statusCode() == 200);
    REQUIRE(result.value().finalUrl == location);
    REQUIRE(result.value().redirectChain.size() == 1);
    REQUIRE(result.value().redirectChain.front() == location);
    REQUIRE(result.value().body().find("downgrade") != std::string_view::npos);
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

    if (!result.isSuccess()) {
        REQUIRE_MSG(false,
            ("IPv6 verified request failed: " + result.message()).c_str());
        return;
    }
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

static void test_https_verify_depth_invalid_value_fails_early() {
    BEGIN_TEST("HttpClient HTTPS verifyDepth < -1 fails with validation "
               "error");

    HttpClient::Options opts;
    opts.connectTimeout = Milliseconds{2000};
    opts.requestTimeout = Milliseconds{2000};
    opts.verifyCertificate = true;
    opts.verifyDepth = -2;
    HttpClient client{opts};

    auto result = client.get("https://127.0.0.1:1/");
    REQUIRE(!result.isSuccess());
    REQUIRE(result.message().find("verifyDepth") != std::string::npos);
}

static void test_https_verify_depth_zero_still_succeeds_for_self_signed_leaf() {
    BEGIN_TEST("HttpClient HTTPS verifyDepth=0 succeeds for trusted "
               "self-signed leaf cert");

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
    opts.verifyDepth = 0;
    HttpClient client{opts};

    const std::string url = "https://127.0.0.1:"
        + std::to_string(server.serverPort().value()) + "/verify-depth-0";
    auto result = client.get(url);

    server.requestStop();
    serverThread.join();

    REQUIRE_MSG(result.isSuccess(),
        ("verifyDepth=0 request failed: " + result.message()).c_str());
    if (!result.isSuccess()) return;
    REQUIRE(result.value().statusCode() == 200);
}

static void test_https_default_system_roots_smoke_gated() {
    BEGIN_TEST("HttpClient HTTPS default system roots smoke test (gated)");

    const std::string gate = getEnvValue_("AISOCKS_RUN_SYSTEM_ROOT_TLS_TEST");
    if (gate != "1") {
        REQUIRE_MSG(
            true, "SKIP - set AISOCKS_RUN_SYSTEM_ROOT_TLS_TEST=1 to enable");
        return;
    }

    HttpClient::Options opts;
    opts.connectTimeout = Milliseconds{4000};
    opts.requestTimeout = Milliseconds{8000};
    opts.verifyCertificate = true;
    HttpClient client{opts};

    auto result = client.get("https://example.com/");
    REQUIRE(result.isSuccess());
    if (!result.isSuccess()) {
        REQUIRE_MSG(
            false, ("system roots smoke failed: " + result.message()).c_str());
        return;
    }
    REQUIRE(result.value().statusCode() >= 200);
    REQUIRE(result.value().statusCode() < 400);
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

static void test_https_handshake_timeout_respects_request_timeout() {
    BEGIN_TEST("HttpClient HTTPS handshake is bounded by requestTimeout");

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
        std::this_thread::sleep_for(std::chrono::milliseconds{400});
    });

    HttpClient::Options opts;
    opts.connectTimeout = Milliseconds{500};
    opts.requestTimeout = Milliseconds{120};
    opts.verifyCertificate = false;
    HttpClient client{opts};

    auto result
        = client.get("https://127.0.0.1:" + std::to_string(port.value()) + "/");

    serverThread.join();

    REQUIRE(!result.isSuccess());
    if (result.message().find("TLS handshake timed out") == std::string::npos) {
        tlsDebugLog_("handshake-timeout test got message: " + result.message());
    }
    REQUIRE_MSG(
        result.message().find("TLS handshake timed out") != std::string::npos,
        ("unexpected TLS error message: " + result.message()).c_str());
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    test_https_cert_load_failure();
    test_https_wrong_port_fails();
    test_https_handshake_timeout_respects_request_timeout();
    test_https_client_basic_get();
    test_https_client_multiple_requests_keep_alive();
    test_https_cache_is_not_reused_for_http_same_host_port();
    test_https_verify_enabled_trusted_ca_and_matching_host();
    test_https_verify_enabled_fails_on_wrong_hostname();
    test_https_verify_enabled_fails_for_untrusted_self_signed();
    test_https_verify_enabled_invalid_ca_file_fails_setup();
    test_https_verify_enabled_invalid_ca_dir_fails_setup();
    test_https_verify_enabled_ca_file_plus_invalid_dir_fails_setup();
    test_https_verify_enabled_ca_dir_only_succeeds();
    test_https_verify_enabled_ca_file_plus_valid_dir_succeeds();
    test_https_redirect_to_different_host_with_verify_enabled();
    test_https_to_http_redirect_behavior();
    test_https_set_options_rebuilds_tls_context();
    test_https_ip_literal_does_not_send_sni();
    test_https_dns_host_sends_sni();
    test_https_verify_enabled_ipv6_san_match_succeeds();
    test_https_verify_enabled_ipv6_san_mismatch_fails();
    test_https_verify_enabled_rejects_non_ascii_dns_host();
    test_https_verify_depth_invalid_value_fails_early();
    test_https_verify_depth_zero_still_succeeds_for_self_signed_leaf();
    test_https_default_system_roots_smoke_gated();

    return test_summary();
}

#else // !AISOCKS_ENABLE_TLS

int main() {
    return 0; /* nothing to test without TLS */
}

#endif // AISOCKS_ENABLE_TLS
