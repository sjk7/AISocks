// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#include "HttpPollServer.h"
#ifdef AISOCKS_ENABLE_TLS
#include "HttpsPollServer.h"
#endif

#include <cstdio>
#include <string>

using namespace aiSocks;

class PlainHelloServer : public HttpPollServer {
    public:
    explicit PlainHelloServer(const ServerBind& bind) : HttpPollServer(bind) {}

    protected:
    void buildResponse(HttpClientState& s) override {
        respondText(s, "Hello from plain HTTP server_launchpad\n");
    }
};

#ifdef AISOCKS_ENABLE_TLS
class SecureHelloServer : public HttpsPollServer {
    public:
    explicit SecureHelloServer(
        const ServerBind& bind, const TlsServerConfig& tls)
        : HttpsPollServer(bind, tls) {}

    protected:
    void buildResponse(HttpClientState& s) override {
        respondText(s, "Hello from HTTPS server_launchpad\n");
    }
};
#endif

int main(int argc, char** argv) {
    if (argc >= 2 && std::string(argv[1]) == "--help") {
        std::printf("server_launchpad usage:\n");
        std::printf("  HTTP : %s [port]\n", argv[0]);
#ifdef AISOCKS_ENABLE_TLS
        std::printf("  HTTPS: %s --tls <cert.pem> <key.pem> [port]\n", argv[0]);
#else
        std::printf("  HTTPS: rebuild with AISOCKS_ENABLE_TLS=ON\n");
#endif
        return 0;
    }

#ifdef AISOCKS_ENABLE_TLS
    if (argc >= 4 && std::string(argv[1]) == "--tls") {
        uint16_t port = 8443;
        if (argc >= 5) {
            port = static_cast<uint16_t>(std::stoi(argv[4]));
        }

        const TlsServerConfig tls{argv[2], argv[3]};
        SecureHelloServer server{ServerBind{"0.0.0.0", Port{port}}, tls};

        if (!server.isValid()) {
            std::fprintf(stderr, "Failed to start HTTPS server on port %u\n",
                static_cast<unsigned>(port));
            return 1;
        }
        if (!server.tlsReady()) {
            std::fprintf(
                stderr, "TLS init failed: %s\n", server.tlsInitError().c_str());
            return 1;
        }

        std::printf("HTTPS server running on https://127.0.0.1:%u/\n",
            static_cast<unsigned>(port));
        server.run(ClientLimit::Unlimited, Milliseconds{5});
        return 0;
    }
#endif

    uint16_t port = 8080;
    if (argc >= 2) {
        port = static_cast<uint16_t>(std::stoi(argv[1]));
    }

    PlainHelloServer server{ServerBind{"0.0.0.0", Port{port}}};
    if (!server.isValid()) {
        std::fprintf(stderr, "Failed to start HTTP server on port %u\n",
            static_cast<unsigned>(port));
        return 1;
    }

    std::printf("HTTP server running on http://127.0.0.1:%u/\n",
        static_cast<unsigned>(port));
    std::printf("Run with --help for TLS mode.\n");
    server.run(ClientLimit::Unlimited, Milliseconds{5});
    return 0;
}
