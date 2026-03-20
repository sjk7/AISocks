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
            // If server requires a client certificate, ensure one was
            // presented.
            if (tlsRequirePeerCert_) {
                const std::string peer
                    = s.tlsSession->getPeerCertificateSubject();
                if (peer.empty()) {
                    const std::string opensslErr
                        = TlsOpenSsl::lastErrorString();
                    std::fprintf(stderr,
                        "[tls] client cert required but none presented "
                        "sslErr=%s\n",
                        opensslErr.empty() ? "<empty>" : opensslErr.c_str());
                    return ServerResult::Disconnect;
                }
            }

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
        if (e != SSL_ERROR_ZERO_RETURN && e != SSL_ERROR_SYSCALL) {
            std::fprintf(stderr,
                "[tls] handshake failed sslErr=%s sslCode=%d\n",
                opensslErr.empty() ? "<empty>" : opensslErr.c_str(), e);
        }
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

        // Optional: enforce that the cert chain file contains more than one
        // certificate (leaf + issuer(s)) when configured to do so. This is a
        // cheap check that counts PEM certificates in the provided file and
        // helps detect incomplete deployments early.
        if (tls.requireFullChain) {
            int count = 0;
            File f;
            if (!f.open(tls.certChainFile.c_str(), "rb")) {
                tlsInitError_ = "failed to open cert chain file for "
                                "requireFullChain check";
                std::fprintf(stderr,
                    "[tls][init] requireFullChain failed cert=%s open failed\n",
                    tls.certChainFile.c_str());
                return;
            }
            const std::vector<char> contents = f.readAll();
            if (contents.empty()) {
                tlsInitError_ = "certificate chain file appears empty";
                std::fprintf(stderr,
                    "[tls][init] requireFullChain failed cert=%s empty\n",
                    tls.certChainFile.c_str());
                return;
            }
            const std::string s(contents.begin(), contents.end());
            size_t pos = 0;
            while (true) {
                pos = s.find("-----BEGIN CERTIFICATE-----", pos);
                if (pos == std::string::npos) break;
                ++count;
                pos += 24; // advance past the match
            }
            if (count < 2) {
                tlsInitError_
                    = "certificate chain file does not contain full chain";
                std::fprintf(stderr,
                    "[tls][init] requireFullChain failed cert=%s count=%d\n",
                    tls.certChainFile.c_str(), count);
                return;
            }
        }

        // Configure client certificate verification when requested.
        if (tls.clientAuth != TlsServerConfig::ClientAuthMode::None) {
            bool verifyPeer = true;
            bool loadDefaults = tls.caFile.empty() && tls.caDir.empty();
            std::string verErr;
            if (!ctx->configureVerifyPeer(verifyPeer, loadDefaults, tls.caFile,
                    tls.caDir,
                    tls.clientAuth == TlsServerConfig::ClientAuthMode::Require,
                    tls.verifyDepth, &verErr)) {
                tlsInitError_
                    = verErr.empty() ? "configureVerifyPeer failed" : verErr;
                return;
            }

            if (tls.clientAuth == TlsServerConfig::ClientAuthMode::Require) {
                tlsRequirePeerCert_ = true;
            }
        }

        tlsContext_ = std::move(ctx);
    }

    std::unique_ptr<TlsContext> tlsContext_;
    std::string tlsInitError_;
    bool tlsRequirePeerCert_{false};
};

} // namespace aiSocks

#endif // AISOCKS_ENABLE_TLS
