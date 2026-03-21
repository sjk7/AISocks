// This is an independent project of an individual developer. Dear PVS-Studio, please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
#include "HttpsFileServer.h"
#include "PathHelper.h"
#include "TlsOpenSsl.h"
#include "test_helpers.h"
#include "SocketFactory.h"

#include <chrono>
#include <optional>
#include <thread>
#include <utility>
#include <openssl/ssl.h>

using namespace aiSocks;

static std::string repoRootFromFile(const char* file) {
    std::string path = PathHelper::normalizePath(file);
    const std::string marker = "/tests/";
    const size_t pos = path.rfind(marker);
    if (pos != std::string::npos) return path.substr(0, pos);
    return ".";
}

// Perform a client handshake and return the TlsSession (owned) on success.
static std::optional<std::pair<std::unique_ptr<TlsSession>, TcpSocket>>
client_connect_tls(int port) {
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

    sess->setConnectState();
    for (;;) {
        int r = sess->handshake();
        if (r == 1)
            return std::make_optional(
                std::make_pair(std::move(sess), std::move(sock)));
        int e = sess->getLastErrorCode(r);
        if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) continue;
        break;
    }
    return std::nullopt;
}

void test_tls_graceful_shutdown() {
    BEGIN_TEST("test_tls_graceful_shutdown");

    const std::string repoRoot = repoRootFromFile(__FILE__);
    const std::string serverCert = repoRoot + "/tests/certs/test_cert.pem";
    const std::string serverKey = repoRoot + "/tests/certs/test_key.pem";

    HttpFileServer::Config cfg;
    cfg.documentRoot = ".";

    TlsServerConfig tls;
    tls.certChainFile = serverCert;
    tls.privateKeyFile = serverKey;
    tls.clientAuth = TlsServerConfig::ClientAuthMode::Optional;

    HttpsFileServer server{ServerBind{"127.0.0.1", Port{0, ""}}, cfg, tls};
    REQUIRE(server.tlsReady());
    std::thread serverThread(
        [&] { server.run(ClientLimit::Unlimited, Milliseconds{1}); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    auto res = client_connect_tls(server.serverPort().value());
    REQUIRE(res.has_value());
    if (!res.has_value()) return; // Extra check for analyzer

    auto sess = std::move(res->first);
    TcpSocket sock = std::move(res->second);

    // Attempt an orderly shutdown from the client side. SSL_shutdown may
    // return 0 (needs to be called again) or 1 (complete). Ensure no fatal
    // error is observed.
    int rc = SSL_shutdown(sess->nativeHandle());
    if (rc == 0) {
        // peer may not have sent close_notify yet; try a short loop.
        for (int i = 0; i < 5 && rc == 0; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            rc = SSL_shutdown(sess->nativeHandle());
        }
    }
    // rc < 0 may indicate the peer closed without a close_notify; accept
    // either 0/1 or an error and continue to verify subsequent writes fail.

    // After shutdown, writes should not succeed.
    const char body[] = "GET / HTTP/1.0\r\nHost: localhost\r\n\r\n";
    int w = sess->write(body, static_cast<int>(sizeof(body) - 1));
    REQUIRE(w <= 0);

    // Cleanup
    sock.close();
    server.requestStop();
    serverThread.join();
}

int main() {
    test_tls_graceful_shutdown();
    return test_summary();
}
