// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#ifndef AISOCKS_DUAL_SERVER_ORCHESTRATOR_H
#define AISOCKS_DUAL_SERVER_ORCHESTRATOR_H

#include "HttpFileServer.h"
#ifdef AISOCKS_ENABLE_TLS
#include "HttpsFileServer.h"
#else
namespace aiSocks {
struct TlsServerConfig {};
} // namespace aiSocks
#endif

#include <memory>
#include <string>

namespace aiSocks {

/**
 * @brief Orchestrates both an HTTP and optional HTTPS file server in a single
 * process.
 *
 * Provides a "compiler firewall" (PImpl) to keep threading and internal
 * server details out of the public header.
 */
class DualServerOrchestrator {
    public:
    struct Ports {
        uint16_t http = 8080;
        uint16_t https = 8443;
        bool enableHttp = true; ///< Set false to skip the HTTP server entirely.
        bool enableHttps
            = true; ///< Set false to skip the HTTPS server entirely.
    };

    /**
     * @brief Construct a new orchestrator.
     *
     * @param ports Ports to listen on.
     * @param config Shared configuration for both servers.
     * @param tls Optional TLS configuration.
     */
    DualServerOrchestrator(const Ports& ports,
        const HttpFileServer::Config& config,
        const TlsServerConfig* tls = nullptr);

    ~DualServerOrchestrator();

    // Disable copy/move for thread safety and PImpl simplicity
    DualServerOrchestrator(const DualServerOrchestrator&) = delete;
    DualServerOrchestrator& operator=(const DualServerOrchestrator&) = delete;

    /**
     * @brief Attach a shared IpFilter to both servers.
     * @param filter Non-owning pointer to the filter.
     */
    void setIpFilter(IpFilter* filter);

    /**
     * @brief Run both servers. Blocks until stop() is called or signals are
     * received.
     * @param limit Connection limit per server.
     * @param timeout Poller timeout.
     */
    void run(ClientLimit limit = ClientLimit::Default,
        Milliseconds timeout = Milliseconds{10});

    /**
     * @brief Gracefully stop both servers.
     */
    void stop();

    /**
     * @brief Check if both servers started successfully.
     */
    bool isValid() const;

    private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};

} // namespace aiSocks

#endif // AISOCKS_DUAL_SERVER_ORCHESTRATOR_H
