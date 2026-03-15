// dual_http_https_server.cpp
// Example: Run both HTTP and HTTPS servers in one process using aiSocks



#include "HttpFileServer.h"
#ifdef AISOCKS_ENABLE_TLS
#include "HttpsFileServer.h"
#endif
#include <thread>
#include <cstdio>
#include <string>

using namespace aiSocks;

int main(int argc, char** argv) {
    std::string wwwRoot = "./www";
    uint16_t httpPort = 8080;
    uint16_t httpsPort = 8443;
    if (argc > 1) {
        wwwRoot = argv[1];
    }
    if (argc > 2) {
        httpPort = static_cast<uint16_t>(std::stoi(argv[2]));
    }
    if (argc > 3) {
        httpsPort = static_cast<uint16_t>(std::stoi(argv[3]));
    }

    // Shared config for both servers
    HttpFileServer::Config config;
    config.documentRoot = wwwRoot;
    config.indexFile = "index.html";
    config.enableDirectoryListing = true;
    config.enableETag = true;
    config.enableLastModified = true;
    config.maxFileSize = 50 * 1024 * 1024;
    config.customHeaders["Server"] = "Dual-HTTP-HTTPS/1.0";
    config.customHeaders["X-Content-Type-Options"] = "nosniff";
    config.customHeaders["X-Frame-Options"] = "DENY";

    // HTTP
    HttpFileServer httpServer(ServerBind{"0.0.0.0", Port{httpPort}}, config);
    std::thread httpThread([&] {
        httpServer.run(ClientLimit::Unlimited, Milliseconds{5});
    });

#ifdef AISOCKS_ENABLE_TLS
    // HTTPS
    TlsServerConfig tls{"server-cert.pem", "server-key.pem"};
    HttpsFileServer httpsServer(ServerBind{"0.0.0.0", Port{httpsPort}}, config, tls);
    std::thread httpsThread([&] {
        httpsServer.run(ClientLimit::Unlimited, Milliseconds{5});
    });
#endif

    httpThread.join();
#ifdef AISOCKS_ENABLE_TLS
    httpsThread.join();
#endif
    return 0;
}
