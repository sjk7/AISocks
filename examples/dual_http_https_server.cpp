// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// dual_http_https_server.cpp
// Example: Run both HTTP and HTTPS servers in one process using DualServerOrchestrator

#include "DualServerOrchestrator.h"
#include <cstdio>
#include <string>

using namespace aiSocks;

int main(int argc, char** argv) {
    std::string wwwRoot = "./www";
    uint16_t httpPort = 8080;
    uint16_t httpsPort = 8443;

    if (argc > 1) wwwRoot = argv[1];
    if (argc > 2) httpPort = static_cast<uint16_t>(std::stoi(argv[2]));
    if (argc > 3) httpsPort = static_cast<uint16_t>(std::stoi(argv[3]));

    // Shared config for both servers
    HttpFileServer::Config config;
    config.documentRoot = wwwRoot;
    config.indexFile = "index.html";
    config.enableDirectoryListing = true;
    config.enableSecurityHeaders = true;
    config.customHeaders["Server"] = "Dual-Orchestrator/1.0";

    DualServerOrchestrator::Ports ports{httpPort, httpsPort};

#ifdef AISOCKS_ENABLE_TLS
    TlsServerConfig tls{"server-cert.pem", "server-key.pem"};
    DualServerOrchestrator orchestrator(ports, config, &tls);
#else
    DualServerOrchestrator orchestrator(ports, config, nullptr);
#endif

    if (!orchestrator.isValid()) {
        fprintf(stderr, "Failed to start servers (check ports or certificates)\n");
        return 1;
    }

    printf("Starting dual servers on port %u (HTTP) and %u (HTTPS)...\n", 
           ports.http, ports.https);

    orchestrator.run(); // Blocks until completion

    return 0;
}

