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

class ReadyHttpsFileServer : public HttpsFileServer {
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

    private:
    std::atomic<bool> ready_{false};
    std::mutex readyMtx_;
    std::condition_variable readyCv_;
};

static std::string repoRootFromFile(const char* file) {
    std::string path = PathHelper::normalizePath(file);
    const std::string marker = "/tests/";
    const size_t pos = path.rfind(marker);
    if (pos != std::string::npos) return path.substr(0, pos);
    return ".";
}

// Helper to perform a raw TLS client handshake, optionally presenting a client
// cert.
static bool do_client_handshake(int port, bool presentClientCert,
    const std::string& clientCert = {}, const std::string& clientKey = {}) {
    ConnectArgs args;
    args.address = "127.0.0.1";
    args.port = Port{static_cast<uint16_t>(port)};
    args.connectTimeout = Milliseconds{2000};
    auto cres = SocketFactory::createTcpClient(args);
    if (!cres.isSuccess()) return false;
    TcpSocket sock = std::move(cres.value());

    std::string err;
    auto ctx = TlsContext::create(TlsContext::Mode::Client, &err);
    if (!ctx) return false;
    auto sess = TlsSession::create(ctx->nativeHandle(), &err);
    if (!sess) return false;
    if (!sess->attachSocket(static_cast<int>(sock.getNativeHandle()), &err))
        return false;

    if (presentClientCert) {
        // Present the client certificate on the SSL object directly.
        if (SSL_use_certificate_file(
                sess->nativeHandle(), clientCert.c_str(), SSL_FILETYPE_PEM)
            != 1)
            return false;
        if (SSL_use_PrivateKey_file(
                sess->nativeHandle(), clientKey.c_str(), SSL_FILETYPE_PEM)
            != 1)
            return false;
    }

    sess->setConnectState();
    for (;;) {
        int r = sess->handshake();
        if (r == 1) {
            // Verify the connection isn't immediately closed by the server
            // (server may accept handshake then drop if client cert missing).
            const char req[] = "GET / HTTP/1.0\r\nHost: localhost\r\n\r\n";
            int w = sess->write(req, static_cast<int>(sizeof(req) - 1));
            if (w <= 0) return false;
            char buf[16];
            int n = sess->read(buf, sizeof(buf));
            if (n <= 0) return false; // closed or no data -> treat as failure
            return true;
        }
        int e = sess->getLastErrorCode(r);
        if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) continue;
        break;
    }
    return false;
}

void test_tls_mtls_accept_reject() {
    BEGIN_TEST("test_tls_mtls_accept_reject");

    const std::string repoRoot = repoRootFromFile(__FILE__);
    const std::string serverCert = repoRoot + "/tests/certs/test_cert.pem";
    const std::string serverKey = repoRoot + "/tests/certs/test_key.pem";
    const std::string clientCert = repoRoot + "/tests/certs/test_cert.pem";
    const std::string clientKey = repoRoot + "/tests/certs/test_key.pem";

    // 1) Server requires client cert: presenting client cert -> handshake OK
    {
        HttpFileServer::Config cfg;
        cfg.documentRoot = ".";

        TlsServerConfig tls;
        tls.certChainFile = serverCert;
        tls.privateKeyFile = serverKey;
        tls.clientAuth = TlsServerConfig::ClientAuthMode::Require;
        tls.caFile = clientCert; // trust the client's self-signed cert

        ReadyHttpsFileServer server{ServerBind{"127.0.0.1", Port{0}}, cfg, tls};
        REQUIRE(server.tlsReady());
        std::thread serverThread(
            [&] { server.run(ClientLimit::Unlimited, Milliseconds{1}); });
        server.waitReady();

        const auto& port = server.serverPort();
        bool ok
            = do_client_handshake(port.value(), true, clientCert, clientKey);
        REQUIRE(ok);

        server.requestStop();
        serverThread.join();
    }

    // 2) Server requires client cert: missing client cert -> handshake fails
    {
        HttpFileServer::Config cfg;
        cfg.documentRoot = ".";

        TlsServerConfig tls;
        tls.certChainFile = serverCert;
        tls.privateKeyFile = serverKey;
        tls.clientAuth = TlsServerConfig::ClientAuthMode::Require;
        tls.caFile = clientCert;

        ReadyHttpsFileServer server{ServerBind{"127.0.0.1", Port{0}}, cfg, tls};
        REQUIRE(server.tlsReady());
        std::thread serverThread(
            [&] { server.run(ClientLimit::Unlimited, Milliseconds{1}); });
        server.waitReady();

        bool ok = do_client_handshake(server.serverPort().value(), false);
        REQUIRE(!ok);

        server.requestStop();
        serverThread.join();
    }

    // 3) Server optional client auth: missing cert -> handshake OK, peer
    // subject empty
    {
        HttpFileServer::Config cfg;
        cfg.documentRoot = ".";

        TlsServerConfig tls;
        tls.certChainFile = serverCert;
        tls.privateKeyFile = serverKey;
        tls.clientAuth = TlsServerConfig::ClientAuthMode::Optional;
        tls.caFile = clientCert;

        ReadyHttpsFileServer server{ServerBind{"127.0.0.1", Port{0}}, cfg, tls};
        REQUIRE(server.tlsReady());
        std::thread serverThread(
            [&] { server.run(ClientLimit::Unlimited, Milliseconds{1}); });
        server.waitReady();

        bool ok = do_client_handshake(server.serverPort().value(), false);
        REQUIRE(ok);

        server.requestStop();
        serverThread.join();
    }
}

int main() {
    test_tls_mtls_accept_reject();
    return test_summary();
}
