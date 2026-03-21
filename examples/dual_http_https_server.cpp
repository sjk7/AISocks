// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// dual_http_https_server.cpp
// Example: Run both HTTP and HTTPS servers in one process using
// DualServerOrchestrator
//
// Usage:
//   dual_http_https_server [--config <file>] [www_root] [http_port]
//   [https_port]
//
// PRECEDENCE (lowest to highest -- later values override earlier ones):
//   1. Built-in defaults (see ServerConf below)
//   2. Config file  -- ./server.conf loaded silently if present, OR
//                      the file given with --config <file> (error if missing)
//   3. Command-line positional args  -- ALWAYS WIN over the config file
//
// Example: if server.conf has  http_port = 9090  but you run:
//   dual_http_https_server ./www 8080
// the port will be 8080, not 9090.
//
// Config file keys (key = value, # comments):
//   www_root          = ./www
//   http_port         = 8080
//   https_port        = 8443
//   cert              = server-cert.pem
//   key               = server-key.pem
//   enable_http       = true    # false -> HTTPS-only
//   enable_https      = true    # false -> HTTP-only
//   index_file        = index.html
//   directory_listing = true
//
// NOTE: enable_http / enable_https can only be set via the config file;
//       there are no positional CLI equivalents for those two flags.
// If no --config flag is given, ./server.conf is tried silently.

#include "DualServerOrchestrator.h"
#include "ServerConf.h"
#include <cstdio>
#include <string>

using namespace aiSocks;

int main(int argc, char** argv) {
    ServerConf conf;

    // Config file: --config <path> wins; otherwise ./server.conf is tried
    // silently.
    int argStart = 1;
    if (argc > 2 && std::string(argv[1]) == "--config") {
        if (!loadServerConf(argv[2], conf)) {
            fprintf(
                stderr, "Error: could not open config file '%s'\n", argv[2]);
            return 1;
        }
        argStart = 3;
    } else {
        loadServerConf("server.conf", conf); // silent — file is optional
    }

    // Command-line positional args take highest precedence -- they always
    // override whatever was read from the config file.  This means you can
    // keep a server.conf for day-to-day defaults and still override specific
    // values on the command line without having to edit the file.
    if (argc > argStart) conf.wwwRoot = argv[argStart];
    if (argc > argStart + 1)
        conf.httpPort = static_cast<uint16_t>(std::stoi(argv[argStart + 1]));
    if (argc > argStart + 2)
        conf.httpsPort = static_cast<uint16_t>(std::stoi(argv[argStart + 2]));

    HttpFileServer::Config httpCfg;
    httpCfg.documentRoot = conf.wwwRoot;
    httpCfg.indexFile = conf.indexFile;
    httpCfg.enableDirectoryListing = conf.directoryListing;
    httpCfg.enableSecurityHeaders = true;
    httpCfg.customHeaders["Server"] = "Dual-Orchestrator/1.0";

    DualServerOrchestrator::Ports ports;
    ports.http = conf.httpPort;
    ports.https = conf.httpsPort;
    ports.enableHttp = conf.enableHttp;
    ports.enableHttps = conf.enableHttps;

#ifdef AISOCKS_ENABLE_TLS
    TlsServerConfig tls{conf.cert, conf.key};
    DualServerOrchestrator orchestrator(ports, httpCfg, &tls);
#else
    DualServerOrchestrator orchestrator(ports, httpCfg, nullptr);
#endif

    if (!orchestrator.isValid()) {
        fprintf(
            stderr, "Failed to start servers (check ports or certificates)\n");
        return 1;
    }

    if (conf.enableHttp) printf("HTTP  listening on port %u\n", conf.httpPort);
    if (conf.enableHttps)
        printf("HTTPS listening on port %u\n", conf.httpsPort);

    orchestrator.run(); // Blocks until SIGINT/SIGTERM or error

    return 0;
}
