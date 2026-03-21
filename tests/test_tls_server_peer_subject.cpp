// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#include "HttpsFileServer.h"
#include "PathHelper.h"
#include "TlsOpenSsl.h"
#include "test_helpers.h"
#include "SocketFactory.h"

#include <chrono>
#include <fstream>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <openssl/ssl.h>
#include <optional>

using namespace aiSocks;

static std::string repoRootFromFile(const char* file) {
    std::string path = PathHelper::normalizePath(file);
    const std::string marker = "/tests/";
    const size_t pos = path.rfind(marker);
    if (pos != std::string::npos) return path.substr(0, pos);
    return ".";
}

// Simple test server that echoes the peer certificate subject as response body.
class PeerSubjectServer : public HttpsFileServer {
    public:
    using HttpsFileServer::HttpsFileServer;

    void waitReady() {
        std::unique_lock<std::mutex> lk(readyMtx_);
        const bool ready = readyCv_.wait_for(
            lk, std::chrono::seconds{2}, [this] { return ready_.load(); });
        REQUIRE_MSG(ready, "server readiness timed out");
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
        // Respond with the peer certificate subject (empty if none).
        respondText(s, s.peerCertSubject);
    }

    private:
    std::atomic<bool> ready_{false};
    std::mutex readyMtx_;
    std::condition_variable readyCv_;
};

// Perform an HTTPS request and return the response body. If handshake fails,
// returns an empty optional.
static std::optional<std::string> do_client_request(int port,
    bool presentClientCert, const std::string& clientCert = {},
    const std::string& clientKey = {}) {
    ConnectArgs args;
    args.address = "127.0.0.1";
    args.port = Port{static_cast<uint16_t>(port)};
    args.connectTimeout = Milliseconds{2000};
    auto cres = SocketFactory::createTcpClient(args);
    if (!cres.isSuccess()) return std::nullopt;
    TcpSocket sock = std::move(cres.value());

    std::string err;
    auto ctx = TlsContext::create(TlsContext::Mode::Client, &err);
    if (!ctx) return std::nullopt;
    auto sess = TlsSession::create(ctx->nativeHandle(), &err);
    if (!sess) return std::nullopt;
    if (!sess->attachSocket(static_cast<int>(sock.getNativeHandle()), &err))
        return std::nullopt;

    if (presentClientCert) {
        if (SSL_use_certificate_file(
                sess->nativeHandle(), clientCert.c_str(), SSL_FILETYPE_PEM)
            != 1)
            return std::nullopt;
        if (SSL_use_PrivateKey_file(
                sess->nativeHandle(), clientKey.c_str(), SSL_FILETYPE_PEM)
            != 1)
            return std::nullopt;
    }

    sess->setConnectState();
    for (;;) {
        int r = sess->handshake();
        if (r == 1) break;
        int e = sess->getLastErrorCode(r);
        if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) continue;
        return std::nullopt;
    }

    const char req[] = "GET / HTTP/1.0\r\nHost: localhost\r\n\r\n";
    if (sess->write(req, static_cast<int>(sizeof(req) - 1)) <= 0)
        return std::nullopt;

    // Read a small response body into a buffer.
    std::string resp;
    char buf[4096];
    int n = sess->read(buf, sizeof(buf));
    if (n <= 0) return std::string{}; // treat as empty body
    resp.append(buf, static_cast<size_t>(n));
    return resp;
}

void test_tls_server_peer_subject() {
    BEGIN_TEST("test_tls_server_peer_subject");

    const std::string repoRoot = repoRootFromFile(__FILE__);
    const std::string serverCert = repoRoot + "/tests/certs/test_cert.pem";
    const std::string serverKey = repoRoot + "/tests/certs/test_key.pem";
    const std::string clientCert
        = repoRoot + "/tests/certs/test_cert_local.pem";
    const std::string clientKey = repoRoot + "/tests/certs/test_key_local.pem";

    // 1) Require client cert, present -> server echoes non-empty subject
    {
        HttpFileServer::Config cfg;
        cfg.documentRoot = ".";

        TlsServerConfig tls;
        tls.certChainFile = serverCert;
        tls.privateKeyFile = serverKey;
        tls.clientAuth = TlsServerConfig::ClientAuthMode::Require;
        tls.caFile = clientCert;

        PeerSubjectServer server{ServerBind{"127.0.0.1", Port{0}}, cfg, tls};
        REQUIRE(server.tlsReady());
        std::thread serverThread(
            [&] { server.run(ClientLimit::Unlimited, Milliseconds{1}); });
        server.waitReady();

        const auto& port = server.serverPort();
        auto resp
            = do_client_request(port.value(), true, clientCert, clientKey);
        REQUIRE(resp.has_value());
        if (resp.has_value()) {
            REQUIRE(!resp->empty());
        }

        server.requestStop();
        serverThread.join();
    }

    // 2) Require client cert, missing -> handshake fails
    {
        HttpFileServer::Config cfg;
        cfg.documentRoot = ".";

        TlsServerConfig tls;
        tls.certChainFile = serverCert;
        tls.privateKeyFile = serverKey;
        tls.clientAuth = TlsServerConfig::ClientAuthMode::Require;
        tls.caFile = clientCert;

        PeerSubjectServer server{ServerBind{"127.0.0.1", Port{0}}, cfg, tls};
        REQUIRE(server.tlsReady());
        std::thread serverThread(
            [&] { server.run(ClientLimit::Unlimited, Milliseconds{1}); });
        server.waitReady();

        auto resp = do_client_request(server.serverPort().value(), false);
        if (resp.has_value()) {
            // Server may accept handshake then immediately disconnect when a
            // required client cert is missing. In that case the response body
            // will be empty. Accept either behavior.
            REQUIRE(resp->empty());
        }

        server.requestStop();
        serverThread.join();
    }

    // 3) Optional client auth, missing -> handshake OK, empty subject
    {
        HttpFileServer::Config cfg;
        cfg.documentRoot = ".";

        TlsServerConfig tls;
        tls.certChainFile = serverCert;
        tls.privateKeyFile = serverKey;
        tls.clientAuth = TlsServerConfig::ClientAuthMode::Optional;
        tls.caFile = clientCert;

        PeerSubjectServer server{ServerBind{"127.0.0.1", Port{0}}, cfg, tls};
        REQUIRE(server.tlsReady());
        std::thread serverThread(
            [&] { server.run(ClientLimit::Unlimited, Milliseconds{1}); });
        server.waitReady();

        auto resp = do_client_request(server.serverPort().value(), false);
        REQUIRE(resp.has_value());
        if (resp.has_value()) {
            // Body may include headers; ensure subject portion is empty by
            // checking the response contains an HTTP status and an empty body
            REQUIRE(resp->find("\r\n\r\n") != std::string::npos);
            const auto pos = resp->find("\r\n\r\n");
            const std::string body = resp->substr(pos + 4);
            REQUIRE(body.empty());
        }

        server.requestStop();
        serverThread.join();
    }
}

int main() {
    test_tls_server_peer_subject();
    return test_summary();
}
