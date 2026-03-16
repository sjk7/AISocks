// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#pragma once

#include "HttpFileServer.h"
#include "HttpsPollServer.h"

#ifdef AISOCKS_ENABLE_TLS

#include <openssl/ssl.h>
#include <memory>
#include <string>

namespace aiSocks {

class HttpsFileServer : public HttpFileServer {
    public:
    explicit HttpsFileServer(const ServerBind& bind, const Config& config,
        const TlsServerConfig& tls, Result<TcpSocket>* result = nullptr)
        : HttpFileServer(bind, config, result) {
        initTls_(tls);
    }

    bool tlsReady() const noexcept { return tlsContext_ != nullptr; }
    const std::string& tlsInitError() const noexcept { return tlsInitError_; }

    protected:
    bool isTlsMode(const HttpClientState& /*s*/) const override {
        return tlsContext_ != nullptr;
    }

    void onTlsClientConnected(TcpSocket& sock, HttpClientState& s) override {
        if (!tlsContext_) return;

        std::string err;
        auto session = TlsSession::create(tlsContext_->nativeHandle(), &err);
        if (!session) return;

        if (!session->attachSocket(
                static_cast<int>(sock.getNativeHandle()), &err)) {
            return;
        }

        session->setAcceptState();
        s.tlsSession = std::move(session);
        s.tlsHandshakeDone = false;
        s.tlsWantsWrite = false;
        s.startTime = std::chrono::steady_clock::now();
    }

    ServerResult doTlsHandshakeStep(
        TcpSocket& /*sock*/, HttpClientState& s) override {
        if (!s.tlsSession) return ServerResult::Disconnect;

        const int r = s.tlsSession->handshake();
        if (r == 1) {
            s.tlsHandshakeDone = true;
            s.tlsWantsWrite = false;
            return ServerResult::KeepConnection;
        }

        const int e = s.tlsSession->getLastErrorCode(r);
        if (e == SSL_ERROR_WANT_READ) {
            s.tlsWantsWrite = false;
            return ServerResult::KeepConnection;
        }
        if (e == SSL_ERROR_WANT_WRITE) {
            s.tlsWantsWrite = true;
            return ServerResult::KeepConnection;
        }

        // Surface OpenSSL error for debugging.
        const std::string opensslErr = TlsOpenSsl::lastErrorString();
        std::fprintf(stderr, "[tls] handshake failed sslErr=%s sslCode=%d\n",
            opensslErr.empty() ? "<empty>" : opensslErr.c_str(), e);
        return ServerResult::Disconnect;
    }

    int tlsRead(TcpSocket& /*sock*/, HttpClientState& s, void* buf,
        size_t len) override {
        if (!s.tlsSession) return -1;
        return s.tlsSession->read(buf, static_cast<int>(len));
    }

    int tlsWrite(TcpSocket& /*sock*/, HttpClientState& s, const char* data,
        size_t len) override {
        if (!s.tlsSession) return 0;
        return s.tlsSession->write(data, static_cast<int>(len));
    }

    private:
    void initTls_(const TlsServerConfig& tls) {
        tlsInitError_.clear();

        if (tls.certChainFile.empty() || tls.privateKeyFile.empty()) {
            tlsInitError_
                = "TLS certChainFile/privateKeyFile must be non-empty";
            return;
        }

        auto ctx = TlsContext::create(TlsContext::Mode::Server, &tlsInitError_);
        if (!ctx) return;

        if (!ctx->loadCertificateChain(
                tls.certChainFile, tls.privateKeyFile, &tlsInitError_)) {
            return;
        }

        tlsContext_ = std::move(ctx);
    }

    std::unique_ptr<TlsContext> tlsContext_;
    std::string tlsInitError_;
};

} // namespace aiSocks

#endif // AISOCKS_ENABLE_TLS
