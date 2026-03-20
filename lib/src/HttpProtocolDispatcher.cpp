#include "HttpProtocolDispatcher.h"
#include <chrono>

#ifdef AISOCKS_ENABLE_TLS
#include <openssl/ssl.h>
#endif

namespace aiSocks {

ServerResult HttpProtocolDispatcher::onReadable(
    TcpSocket& sock, HttpClientState& s) {
    char buf[HttpPollServer::RECV_BUF_SIZE];
    for (;;) {
        ServerResult stageOut = ServerResult::KeepConnection;
        if (server_.runTlsHandshakeStage_(sock, s, stageOut)) return stageOut;

        const auto now = std::chrono::steady_clock::now();
        const int effectiveSlowloris
            = (server_.clientCount()
                  > HttpPollServer::SLOWLORIS_HIGH_LOAD_THRESHOLD)
            ? HttpPollServer::SLOWLORIS_TIMEOUT_MS_HIGH_LOAD
            : server_.slowlorisTimeoutMs_;

        if (server_.slowlorisExpired(s, now, effectiveSlowloris))
            return ServerResult::Disconnect;

        int n = server_.tlsRead(sock, s, buf, sizeof(buf));

        if (n > 0) {
            server_.touchClient(sock, now);

            if (!s.dataView.empty()) {
                s.queuedRequest.append(buf, static_cast<size_t>(n));
                continue;
            }

            if (s.request.empty()) {
                s.startTime = now;
            }
            s.request.append(buf, static_cast<size_t>(n));

            if (s.dataView.empty()) {
                if (server_.runRequestFrameInspectionStage_(sock, s, stageOut))
                    return stageOut;
            }
        } else if (n == 0) {
            return ServerResult::Disconnect;
        } else {
#ifdef AISOCKS_ENABLE_TLS
            if (server_.isTlsMode(s) && s.tlsSession) {
                const int tlsErr = s.tlsSession->getLastErrorCode(n);
                if (tlsErr == SSL_ERROR_WANT_READ
                    || tlsErr == SSL_ERROR_WANT_WRITE) {
                    server_.setClientWritable(
                        sock, tlsErr == SSL_ERROR_WANT_WRITE);
                    break;
                }
            }
#endif
            const auto err = sock.getLastError();
            if (err == SocketError::WouldBlock || err == SocketError::Timeout)
                break;
            return ServerResult::Disconnect;
        }
    }
    return ServerResult::KeepConnection;
}

ServerResult HttpProtocolDispatcher::onWritable(
    TcpSocket& sock, HttpClientState& s) {
    while (true) {
        ServerResult stageOut = ServerResult::KeepConnection;
        if (server_.runTlsHandshakeStage_(sock, s, stageOut)) return stageOut;
        if (s.dataView.empty()) return ServerResult::KeepConnection;
        if (server_.runSendStreamStage_(sock, s, stageOut)) return stageOut;
        if (server_.runPipelineContinuationStage_(sock, s, stageOut))
            return stageOut;
    }
}

} // namespace aiSocks
