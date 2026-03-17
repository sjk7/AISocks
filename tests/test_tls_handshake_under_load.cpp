#include "HttpsFileServer.h"
#include "TlsOpenSsl.h"
#include "test_helpers.h"
#include "SocketFactory.h"

#include <chrono>
#include <fstream>
#include <thread>
#include <vector>
#include <filesystem>
#include <iostream>

using namespace aiSocks;

void test_https_server_handshake_timeout_under_load() {
    BEGIN_TEST("test_https_server_handshake_timeout_under_load");

    // Create a minimal document root
    const std::string docRoot = "./test_tmp_docroot";
    std::filesystem::create_directories(docRoot);
    std::ofstream out(docRoot + "/index.html");
    out << "hello";
    out.close();

    HttpFileServer::Config cfg;
    cfg.documentRoot = docRoot;
    cfg.indexFile = "index.html";

    // Always resolve cert/key relative to the source root, not build dir
    // Always use path relative to the source directory, not build dir
    // Use AISOCKS_SOURCE_DIR env variable if available
    const char* envSourceDir = std::getenv("AISOCKS_SOURCE_DIR");
    std::string sourceDir = envSourceDir ? envSourceDir : std::filesystem::absolute(".").string();
    const std::string cert = sourceDir + "/tests/certs/test_cert.pem";
    const std::string key = sourceDir + "/tests/certs/test_key.pem";
    std::cerr << "[DEBUG] Using cert: " << cert << "\n[DEBUG] Using key: " << key << std::endl;

    TlsServerConfig tls;
    tls.certChainFile = cert;
    tls.privateKeyFile = key;
    tls.handshakeTimeoutMs = 120; // short timeout for test

    HttpsFileServer server{ServerBind{"127.0.0.1", Port{0}}, cfg, tls};
    if (!server.tlsReady()) {
        std::cerr << "TLS server failed to initialize. Cert: " << cert << ", Key: " << key << std::endl;
        std::cerr << "Check file existence, permissions, and OpenSSL errors." << std::endl;
        REQUIRE(false);
    }

    std::thread serverThread(
        [&] { server.run(ClientLimit::Unlimited, Milliseconds{5}); });
    // Wait for server to bind and be ready by briefly sleeping.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    const int N = 6;
    std::vector<TcpSocket> clients;
    clients.reserve(N);

    for (int i = 0; i < N; ++i) {
        ConnectArgs args;
        args.address = "127.0.0.1";
        args.port = server.serverPort();
        args.connectTimeout = Milliseconds{500};
        auto start = std::chrono::steady_clock::now();
        auto res = SocketFactory::createTcpClient(args);
        auto end = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            if (!res.isSuccess()) {
                std::cerr << "[HANDSHAKE] Client " << i << " failed to connect in " << ms << " ms: " << res.message() << std::endl;
            } else {
            std::cerr << "[HANDSHAKE] Client " << i << " connected in " << ms << " ms" << std::endl;
            TcpSocket s = std::move(res.value());
            s.setReceiveTimeout(Milliseconds{500});
            clients.push_back(std::move(s));
        }
    }

    // Wait for stalled handshakes, but check every 1ms for faster loop
    for (int i = 0; i < 300; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    for (auto& c : clients) {
        char buf[8];
        int n = c.receive(buf, sizeof(buf));
        if (n > 0) {
            std::cerr << "[RECV] received " << n << " bytes" << std::endl;
        } else {
            std::cerr << "[RECV] connection closed or error: " << n << std::endl;
        }
        REQUIRE(n <= 0);
        c.close();
    }

    server.requestStop();
    serverThread.join();
}

int main() {
    test_https_server_handshake_timeout_under_load();
    return test_summary();
}
