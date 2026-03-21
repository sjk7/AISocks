// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#include "DualServerOrchestrator.h"
#include <thread>
#include <vector>
#include <cstdio>
#include <atomic>

namespace aiSocks {

struct DualServerOrchestrator::Impl {
    std::unique_ptr<HttpFileServer> httpServer;
#ifdef AISOCKS_ENABLE_TLS
    std::unique_ptr<HttpsFileServer> httpsServer;
#endif
    std::atomic<bool> running{false};

    Impl(const Ports& ports, const HttpFileServer::Config& config,
        const TlsServerConfig* tls) {
        if (ports.enableHttp) {
            ServerBind httpBind;
            httpBind.address = "0.0.0.0";
            httpBind.port = Port{ports.http};
            httpBind.serverName = "HTTP";
            httpServer = std::make_unique<HttpFileServer>(httpBind, config);
        }
#ifdef AISOCKS_ENABLE_TLS
        if (tls && ports.enableHttps) {
            ServerBind httpsBind;
            httpsBind.address = "0.0.0.0";
            httpsBind.port = Port{ports.https};
            httpsBind.serverName = "HTTPS";
            httpsServer
                = std::make_unique<HttpsFileServer>(httpsBind, config, *tls);
        }
#else
        (void)tls; // Silence unused parameter warning on non-TLS builds
#endif
    }
};

DualServerOrchestrator::DualServerOrchestrator(const Ports& ports,
    const HttpFileServer::Config& config, const TlsServerConfig* tls)
    : pimpl_(std::make_unique<Impl>(ports, config, tls)) {}

DualServerOrchestrator::~DualServerOrchestrator() {
    stop();
}

void DualServerOrchestrator::setIpFilter(IpFilter* filter) {
    if (pimpl_->httpServer) pimpl_->httpServer->setIpFilter(filter);
#ifdef AISOCKS_ENABLE_TLS
    if (pimpl_->httpsServer) pimpl_->httpsServer->setIpFilter(filter);
#endif
}

void DualServerOrchestrator::run(ClientLimit limit, Milliseconds timeout) {
    if (pimpl_->running.exchange(true)) return;

    std::vector<std::thread> threads;
    if (pimpl_->httpServer && pimpl_->httpServer->isValid()) {
        threads.emplace_back([&] { pimpl_->httpServer->run(limit, timeout); });
    }

#ifdef AISOCKS_ENABLE_TLS
    if (pimpl_->httpsServer && pimpl_->httpsServer->isValid()) {
        threads.emplace_back([&] { pimpl_->httpsServer->run(limit, timeout); });
    }
#endif

    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }
    pimpl_->running = false;
}

void DualServerOrchestrator::stop() {
    if (pimpl_->httpServer) pimpl_->httpServer->requestStop();
#ifdef AISOCKS_ENABLE_TLS
    if (pimpl_->httpsServer) pimpl_->httpsServer->requestStop();
#endif
}

bool DualServerOrchestrator::isValid() const {
    // Every server that was requested (non-null) must have started
    // successfully, and at least one server must have been requested.
    bool anyRequested = false;
    if (pimpl_->httpServer) {
        anyRequested = true;
        if (!pimpl_->httpServer->isValid()) return false;
    }
#ifdef AISOCKS_ENABLE_TLS
    if (pimpl_->httpsServer) {
        anyRequested = true;
        if (!pimpl_->httpsServer->isValid()) return false;
    }
#endif
    return anyRequested;
}

} // namespace aiSocks
