#include "HttpsFileServer.h"
#include "TlsOpenSsl.h"
#include "test_helpers.h"
#include "SocketFactory.h"

#include <chrono>
#include <fstream>
#include <thread>
#include <vector>

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

    // Derive repo root from __FILE__ so cert paths work regardless of CTest cwd
    std::string filePath = std::filesystem::path(__FILE__).generic_string();
    const std::string marker = "/tests/";
    std::string repoRoot;
    const size_t pos = filePath.rfind(marker);
    if (pos != std::string::npos) {
        repoRoot = filePath.substr(0, pos);
    } else {
        repoRoot = ".";
    }

    const std::string cert = repoRoot + "/tests/certs/test_cert.pem";
    const std::string key = repoRoot + "/tests/certs/test_key.pem";

    TlsServerConfig tls;
    tls.certChainFile = cert;
    tls.privateKeyFile = key;
    tls.handshakeTimeoutMs = 120; // short timeout for test

    HttpsFileServer server{ServerBind{"127.0.0.1", Port{0}}, cfg, tls};
    REQUIRE(server.tlsReady());

    std::thread serverThread(
        [&] { server.run(ClientLimit::Unlimited, Milliseconds{5}); });
    // Wait for server to bind and be ready by briefly sleeping.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    const int N = 40;
    std::vector<TcpSocket> clients;
    clients.reserve(N);

    for (int i = 0; i < N; ++i) {
        ConnectArgs args;
        args.address = "127.0.0.1";
        args.port = server.serverPort();
        args.connectTimeout = Milliseconds{500};

        auto res = SocketFactory::createTcpClient(args);
        REQUIRE(res.isSuccess());
        TcpSocket s = std::move(res.value());
        s.setReceiveTimeout(Milliseconds{500});
        clients.push_back(std::move(s));
    }

    // Wait longer than the server handshake timeout so stalled handshakes
    // should be dropped.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    for (auto& c : clients) {
        char buf[8];
        int n = c.receive(buf, sizeof(buf));
        // Expect the server to have closed the connection (recv==0) or an
        // error indicating the socket was closed.
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
