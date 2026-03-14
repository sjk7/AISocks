// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it. PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#include "HttpPollServer.h"

#include "BuildInfo.h"
#include "HttpParserUtils.h"
#include "HttpRequest.h"
#include <charconv>
#include <cctype>
#include <cstdio>
#include <string>

#ifdef AISOCKS_ENABLE_TLS
#include <openssl/ssl.h>
#endif

namespace aiSocks {

namespace {

    constexpr size_t kMaxRequestBodyBytes = 16 * 1024 * 1024;

    enum class RequestFrameStatus {
        NeedMore,
        Complete,
        BadRequest,
        UnsupportedTransferEncoding,
        HeaderTooLarge,
        BodyTooLarge
    };

    static bool parseContentLength_(
        std::string_view value, size_t& out, bool& overflow) {
        overflow = false;
        if (value.empty()) return false;
        for (char ch : value) {
            if (ch < '0' || ch > '9') return false;
        }
        unsigned long long parsed = 0;
        auto [ptr, ec] = std::from_chars(
            value.data(), value.data() + value.size(), parsed);
        if (ec != std::errc{} || ptr != value.data() + value.size())
            return false;
        if (parsed > static_cast<unsigned long long>(SIZE_MAX)) {
            overflow = true;
            return false;
        }
        out = static_cast<size_t>(parsed);
        return true;
    }

    static bool parseChunkSize_(std::string_view sizeLine, size_t& out) {
        const size_t extPos = sizeLine.find(';');
        const std::string_view hexStr = (extPos == std::string_view::npos)
            ? sizeLine
            : sizeLine.substr(0, extPos);
        if (hexStr.empty()) return false;

        out = 0;
        for (char c : hexStr) {
            const unsigned char uc = static_cast<unsigned char>(c);
            size_t digit = 0;
            if (uc >= '0' && uc <= '9')
                digit = static_cast<size_t>(uc - '0');
            else if (uc >= 'a' && uc <= 'f')
                digit = static_cast<size_t>(uc - 'a' + 10);
            else if (uc >= 'A' && uc <= 'F')
                digit = static_cast<size_t>(uc - 'A' + 10);
            else
                return false;

            if (out > (kMaxRequestBodyBytes - digit) / 16) return false;
            out = out * 16 + digit;
        }
        return true;
    }

    static bool transferEncodingEndsInChunked_(std::string_view value) {
        size_t start = 0;
        std::string_view last;
        while (start < value.size()) {
            const size_t comma = value.find(',', start);
            const size_t end
                = (comma == std::string_view::npos) ? value.size() : comma;
            std::string_view tok = value.substr(start, end - start);
            const size_t ts = tok.find_first_not_of(" \t");
            if (ts != std::string_view::npos) tok = tok.substr(ts);
            const size_t te = tok.find_last_not_of(" \t");
            if (te != std::string_view::npos)
                tok = tok.substr(0, te + 1);
            else
                tok = {};
            if (!tok.empty()) last = tok;
            if (comma == std::string_view::npos) break;
            start = comma + 1;
        }
        if (last.size() != 7) return false;
        return std::tolower(static_cast<unsigned char>(last[0])) == 'c'
            && std::tolower(static_cast<unsigned char>(last[1])) == 'h'
            && std::tolower(static_cast<unsigned char>(last[2])) == 'u'
            && std::tolower(static_cast<unsigned char>(last[3])) == 'n'
            && std::tolower(static_cast<unsigned char>(last[4])) == 'k'
            && std::tolower(static_cast<unsigned char>(last[5])) == 'e'
            && std::tolower(static_cast<unsigned char>(last[6])) == 'd';
    }

    static bool ciTokenPresent_(
        std::string_view field, std::string_view token) {
        const size_t tlen = token.size();
        size_t pos = 0;
        while (pos < field.size()) {
            const size_t comma = field.find(',', pos);
            const size_t end
                = (comma == std::string_view::npos) ? field.size() : comma;
            std::string_view t = field.substr(pos, end - pos);
            const size_t ts = t.find_first_not_of(" \t");
            if (ts != std::string_view::npos) t = t.substr(ts);
            const size_t te = t.find_last_not_of(" \t");
            if (te != std::string_view::npos)
                t = t.substr(0, te + 1);
            else
                t = {};

            if (t.size() == tlen) {
                bool match = true;
                for (size_t i = 0; i < tlen; ++i) {
                    if (std::tolower(static_cast<unsigned char>(t[i]))
                        != std::tolower(static_cast<unsigned char>(token[i]))) {
                        match = false;
                        break;
                    }
                }
                if (match) return true;
            }
            pos = (comma == std::string_view::npos) ? field.size() : comma + 1;
        }
        return false;
    }

    static bool shouldSend100Continue_(
        std::string_view req, RequestFrameStatus frame) {
        if (frame != RequestFrameStatus::NeedMore) return false;

        const auto [sep, sepLen] = detail::findHeaderBodySep(req);
        if (sep == std::string_view::npos || sepLen == 0) return false;

        const std::string_view headerSection = req.substr(0, sep);
        const auto [requestLine, firstNL]
            = detail::extractFirstLine(headerSection);
        if (requestLine.empty() || firstNL == std::string_view::npos)
            return false;

        bool expect100Continue = false;
        detail::parseHeaderFields(
            headerSection, firstNL, [&](std::string key, std::string_view val) {
                if (key == "expect" && ciTokenPresent_(val, "100-continue"))
                    expect100Continue = true;
            });
        return expect100Continue;
    }

    static RequestFrameStatus inspectRequestFrame_(std::string_view req,
        size_t& consumedLen,
        size_t maxHeaderBytes = HttpPollServer::MAX_HEADER_SIZE,
        size_t maxBodyBytes = kMaxRequestBodyBytes) {
        consumedLen = 0;

        const auto [sep, sepLen] = detail::findHeaderBodySep(req);
        if (sep == std::string_view::npos) {
            if (req.size() > maxHeaderBytes)
                return RequestFrameStatus::HeaderTooLarge;
            return RequestFrameStatus::NeedMore;
        }
        if (sep > maxHeaderBytes) return RequestFrameStatus::HeaderTooLarge;

        const size_t headerBytes = sep + sepLen;
        const std::string_view headerSection = req.substr(0, sep);
        const std::string_view body = req.substr(headerBytes);

        bool hasTransferEncoding = false;
        std::string transferEncodingValue;
        bool hasContentLength = false;
        size_t contentLength = 0;
        bool contentLengthOverflow = false;

        const auto [requestLine, firstNL]
            = detail::extractFirstLine(headerSection);
        if (requestLine.empty()) return RequestFrameStatus::BadRequest;

        if (firstNL != std::string_view::npos) {
            detail::parseHeaderFields(headerSection, firstNL,
                [&](std::string key, std::string_view val) {
                    if (key == "transfer-encoding") {
                        if (!transferEncodingValue.empty())
                            transferEncodingValue.append(",");
                        transferEncodingValue.append(val.data(), val.size());
                        hasTransferEncoding = true;
                    } else if (key == "content-length") {
                        size_t parsed = 0;
                        bool overflow = false;
                        if (!parseContentLength_(val, parsed, overflow)) {
                            contentLengthOverflow = overflow;
                            hasContentLength = true;
                            contentLength = 0;
                            return;
                        }
                        if (hasContentLength && contentLength != parsed) {
                            // Ambiguous duplicate Content-Length values.
                            contentLengthOverflow = true;
                            return;
                        }
                        hasContentLength = true;
                        contentLength = parsed;
                    }
                });
        }

        if (contentLengthOverflow) return RequestFrameStatus::BadRequest;
        if (hasContentLength && contentLength > maxBodyBytes)
            return RequestFrameStatus::BodyTooLarge;

        if (hasTransferEncoding) {
            if (hasContentLength) return RequestFrameStatus::BadRequest;
            if (!transferEncodingEndsInChunked_(transferEncodingValue))
                return RequestFrameStatus::UnsupportedTransferEncoding;

            size_t pos = 0;
            size_t decodedTotal = 0;
            while (true) {
                const size_t crlfPos = body.find("\r\n", pos);
                if (crlfPos == std::string_view::npos)
                    return RequestFrameStatus::NeedMore;

                const std::string_view sizeLine
                    = body.substr(pos, crlfPos - pos);
                size_t chunkSize = 0;
                if (!parseChunkSize_(sizeLine, chunkSize))
                    return RequestFrameStatus::BadRequest;

                if (chunkSize == 0) {
                    const size_t trailerEnd = body.find("\r\n\r\n", crlfPos);
                    if (trailerEnd == std::string_view::npos)
                        return RequestFrameStatus::NeedMore;
                    consumedLen = headerBytes + trailerEnd + 4;
                    return RequestFrameStatus::Complete;
                }

                if (decodedTotal > maxBodyBytes - chunkSize)
                    return RequestFrameStatus::BodyTooLarge;
                decodedTotal += chunkSize;

                const size_t dataStart = crlfPos + 2;
                const size_t dataEnd = dataStart + chunkSize;
                const size_t nextChunk = dataEnd + 2;
                if (nextChunk > body.size())
                    return RequestFrameStatus::NeedMore;
                if (body[dataEnd] != '\r' || body[dataEnd + 1] != '\n')
                    return RequestFrameStatus::BadRequest;
                pos = nextChunk;
            }
        }

        if (hasContentLength) {
            if (body.size() < contentLength)
                return RequestFrameStatus::NeedMore;
            consumedLen = headerBytes + contentLength;
            return RequestFrameStatus::Complete;
        }

        consumedLen = headerBytes;
        return RequestFrameStatus::Complete;
    }

} // namespace

void HttpPollServer::run(ClientLimit maxClients, Milliseconds timeout) {
#ifndef NDEBUG
    printf("[DEBUG] HttpPollServer::run() - Starting server\n");
    fflush(stdout);
#endif

    ServerBase<HttpClientState>::run(maxClients, timeout);
    if (accessLogger_) accessLogger_->flush(); // drain any buffered log entries

#ifndef NDEBUG
    printf("[DEBUG] HttpPollServer::run() - Server completed\n");
    fflush(stdout);
#endif

    printf("\nServer stopped gracefully.\n");
}

void HttpPollServer::printBuildInfo() {
    BuildInfo::print();
}

void HttpPollServer::printStartupBanner() {
    // Default implementation prints build info.
    // Subclasses can override and call HttpPollServer::printStartupBanner()
    // to include build info alongside custom startup messages.
    BuildInfo::print();
}

// ---------------------------------------------------------------------------
// onClientConnected — store peer IP in per-connection state
// ---------------------------------------------------------------------------
void HttpPollServer::onClientConnected(TcpSocket& sock, HttpClientState& s) {
    auto ep = sock.getPeerEndpoint();
    if (ep.isSuccess()) s.peerAddress = ep.value().address;
    if (isTlsMode(s)) onTlsClientConnected(sock, s);
}

// ---------------------------------------------------------------------------
// onAcceptFilter — IP-level blocking before poller registration
// ---------------------------------------------------------------------------
bool HttpPollServer::onAcceptFilter(const std::string& peerAddress) {
    if (ipFilter_ && !ipFilter_->isAllowed(peerAddress)) return false;
    return true;
}

// ---------------------------------------------------------------------------
// onResponseSent — Apache Combined Log entry
// ---------------------------------------------------------------------------
void HttpPollServer::onResponseSent(HttpClientState& s) {
    if (!accessLogger_) return;
    const std::string requestLine = AccessLogger::extractRequestLine(s.request);
    const int statusCode = AccessLogger::extractStatusCode(s.responseView);
    accessLogger_->log(
        s.peerAddress, requestLine, statusCode, s.responseView.size());
}

ServerResult HttpPollServer::onError(TcpSocket& sock, HttpClientState& /*s*/) {
    auto err = sock.getLastError();
    // Poll systems can report error events with SO_ERROR==0 for connection
    // resets or other conditions. This is not a real error and should not
    // be logged.
    if (err != SocketError::None) {
        printf("[error] poll error on client socket: code=%d msg=%s\n",
            static_cast<int>(err), sock.getErrorMessage().c_str());
    }
    return ServerResult::Disconnect;
}

bool HttpPollServer::isHttpRequest(const std::string& req) {
    auto startsWith = [&](std::string_view method) noexcept {
        return req.size() > method.size()
            && req.compare(0, method.size(), method) == 0
            && req[method.size()] == ' ';
    };
    return startsWith("GET") || startsWith("POST") || startsWith("PUT")
        || startsWith("HEAD") || startsWith("DELETE") || startsWith("OPTIONS")
        || startsWith("PATCH");
}

bool HttpPollServer::requestComplete(const std::string& req) {
    size_t consumed = 0;
    const RequestFrameStatus s
        = inspectRequestFrame_(req, consumed, HttpPollServer::MAX_HEADER_SIZE);
    return s != RequestFrameStatus::NeedMore;
}

bool HttpPollServer::requestComplete(const std::string& req, size_t& scanPos) {
    const size_t start = scanPos >= 3 ? scanPos - 3 : 0;
    const auto [sep, sepLen]
        = aiSocks::detail::findHeaderBodySep(std::string_view(req), start);
    (void)sepLen;
    if (sep != std::string_view::npos) {
        size_t consumed = 0;
        const RequestFrameStatus s = inspectRequestFrame_(
            req, consumed, HttpPollServer::MAX_HEADER_SIZE);
        if (s == RequestFrameStatus::NeedMore) {
            scanPos = req.size() >= 3 ? req.size() - 3 : 0;
            return false;
        }
        return true;
    }
    scanPos = req.size() >= 3 ? req.size() - 3 : 0;
    return false;
}

// Consume one complete HTTP/1.x request message from the front of req.
// Returns true on success and writes the consumed length.
static bool consumeRequestMessage_(
    const std::string& req, size_t& consumedLen) {
    const RequestFrameStatus s = inspectRequestFrame_(
        std::string_view(req), consumedLen, HttpPollServer::MAX_HEADER_SIZE);
    return s == RequestFrameStatus::Complete;
}

std::string HttpPollServer::makeResponse(const char* statusLine,
    const char* contentType, const std::string& body, bool keepAlive) {
    std::string r;
    r.reserve(256 + body.size());
    r += statusLine;
    r += "\r\nContent-Type: ";
    r += contentType;
    r += "\r\nContent-Length: ";
    char lenBuf[20];
    snprintf(lenBuf, sizeof(lenBuf), "%zu", body.size());
    r += lenBuf;
    r += keepAlive ? "\r\nConnection: keep-alive\r\n\r\n"
                   : "\r\nConnection: close\r\n\r\n";
    r += body;
    return r;
}

// HTTP/1.0 defaults to close; HTTP/1.1 defaults to keep-alive (RFC 7230 §6.3).
// The Connection header can override either default.
static bool resolveKeepAlive_(const HttpRequest& req) {
    const bool http10 = req.version == "HTTP/1.0";
    const std::string* conn = req.header("connection");
    const bool hasKeepAlive = conn && ciTokenPresent_(*conn, "keep-alive");
    const bool hasClose = conn && ciTokenPresent_(*conn, "close");
    return http10 ? !hasKeepAlive : hasClose;
}

// Returns true when the slowloris deadline has expired: more than `timeoutMs`
// milliseconds have elapsed since the first byte arrived but headers are still
// incomplete. `now` is passed in from the recv loop (already computed once per
// iteration).
static bool isSlowlorisTimeout_(const HttpClientState& s,
    std::chrono::steady_clock::time_point now, int timeoutMs) {
    if (!s.responseView.empty()) return false; // already responding
    if (s.request.empty()) return false;
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - s.startTime);
    return elapsed.count() > timeoutMs;
}

void HttpPollServer::dispatchBuildResponse(HttpClientState& s) {
    // Fast method gate for unsupported methods: keeps behavior stable even
    // when request bodies are not yet buffered for non-GET/HEAD requests.
    const size_t sp = s.request.find(' ');
    if (sp != std::string::npos && sp > 0) {
        const std::string_view m{s.request.data(), sp};
        if (m != "GET" && m != "HEAD") {
            s.responseBuf = makeResponse("HTTP/1.1 405 Method Not Allowed",
                "text/plain; charset=utf-8",
                "405 Method Not Allowed\nOnly GET and HEAD are supported.\n",
                false);
            s.responseView = s.responseBuf;
            s.closeAfterSend = true;
            s.parsedRequest = HttpRequest{};
            return;
        }
    }

    auto req = HttpRequest::parse(s.request);
    if (!req.valid) {
        s.responseBuf = makeResponse("HTTP/1.1 400 Bad Request",
            "text/plain; charset=utf-8", "400 Bad Request\n", false);
        s.responseView = s.responseBuf;
        s.closeAfterSend = true;
        s.parsedRequest = HttpRequest{};
        return;
    }

    // Determine connection semantics and cache the parse result so
    // buildResponse() overrides do not need to re-parse the same bytes.
    if (req.version == "HTTP/1.1" && !req.header("host")) {
        s.responseBuf = makeResponse("HTTP/1.1 400 Bad Request",
            "text/plain; charset=utf-8",
            "400 Bad Request\nHost header required for HTTP/1.1.\n", false);
        s.responseView = s.responseBuf;
        s.closeAfterSend = true;
        s.parsedRequest = HttpRequest{};
        return;
    }

    if (const std::string* expect = req.header("expect")) {
        if (!ciTokenPresent_(*expect, "100-continue")) {
            s.responseBuf = makeResponse("HTTP/1.1 417 Expectation Failed",
                "text/plain; charset=utf-8", "417 Expectation Failed\n", false);
            s.responseView = s.responseBuf;
            s.closeAfterSend = true;
            s.parsedRequest = HttpRequest{};
            return;
        }
    }

    s.closeAfterSend = resolveKeepAlive_(req);
    s.parsedRequest = std::move(req);

    // Record this request for rate tracking; reject if now auto-blacklisted.
    if (ipFilter_) {
        ipFilter_->recordRequest(s.peerAddress);
        if (!ipFilter_->isAllowed(s.peerAddress)) {
            s.responseBuf = makeResponse("HTTP/1.1 403 Forbidden",
                "text/plain; charset=utf-8", "403 Forbidden\nAccess denied.\n",
                false);
            s.responseView = s.responseBuf;
            s.closeAfterSend = true;
            s.parsedRequest = HttpRequest{};
            return;
        }
    }

    buildResponse(s);
}

ServerResult HttpPollServer::onReadable(TcpSocket& sock, HttpClientState& s) {
    char buf[RECV_BUF_SIZE];
    for (;;) {
#ifdef AISOCKS_ENABLE_TLS
        if (isTlsMode(s) && !s.tlsHandshakeDone) {
            const ServerResult hs = doTlsHandshakeStep(sock, s);
            if (hs != ServerResult::KeepConnection) return hs;
            if (!s.tlsHandshakeDone) {
                setClientWritable(sock, s.tlsWantsWrite);
                return ServerResult::KeepConnection;
            }
            setClientWritable(sock, false);
        }
#endif

        // Slowloris protection: drop if headers not received within 5 seconds.
        // Clock is read once per loop iteration and reused by the timeout
        // check.
        const auto now = std::chrono::steady_clock::now();
        // Use a tighter slowloris window under high load to shed stalled
        // partial-request senders faster and reduce tail latency.
        const int effectiveSlowloris
            = (clientCount() > SLOWLORIS_HIGH_LOAD_THRESHOLD)
            ? SLOWLORIS_TIMEOUT_MS_HIGH_LOAD
            : slowlorisTimeoutMs_;
        if (isSlowlorisTimeout_(s, now, effectiveSlowloris))
            return ServerResult::Disconnect;

        int n = tlsRead(sock, s, buf, sizeof(buf));

        if (n > 0) {
            touchClient(sock, now);

            // If a response is already in-flight, park incoming bytes as the
            // next pipelined request payload and process them after send.
            if (!s.responseView.empty()) {
                s.queuedRequest.append(buf, static_cast<size_t>(n));
                continue;
            }

            if (s.request.empty()) {
                // Start a new slowloris window for each new request.
                s.startTime = now;
            }
            s.request.append(buf, static_cast<size_t>(n));

            if (s.responseView.empty()) {
                size_t consumed = 0;
                const RequestFrameStatus frame
                    = inspectRequestFrame_(std::string_view(s.request),
                        consumed, HttpPollServer::MAX_HEADER_SIZE);
                if (frame == RequestFrameStatus::NeedMore) {
                    if (!s.expectContinueSent
                        && shouldSend100Continue_(s.request, frame)) {
                        s.responseBuf = "HTTP/1.1 100 Continue\r\n\r\n";
                        s.responseView = s.responseBuf;
                        s.sent = 0;
                        s.responseStarted = false;
                        s.closeAfterSend = false;
                        s.interimResponse = true;
                        s.expectContinueSent = true;
                        setClientWritable(sock, true);
                        return ServerResult::KeepConnection;
                    }
                    continue;
                }

                if (frame == RequestFrameStatus::HeaderTooLarge) {
                    s.responseBuf = makeResponse(
                        "HTTP/1.1 431 Request Header Fields Too Large",
                        "text/plain; charset=utf-8",
                        "Header section too large.\n", false);
                    s.responseView = s.responseBuf;
                    s.closeAfterSend = true;
                    setClientWritable(sock, true);
                    return ServerResult::KeepConnection;
                }

                if (frame == RequestFrameStatus::BodyTooLarge) {
                    s.responseBuf
                        = makeResponse("HTTP/1.1 413 Payload Too Large",
                            "text/plain; charset=utf-8",
                            "Request body is too large.\n", false);
                    s.responseView = s.responseBuf;
                    s.closeAfterSend = true;
                    setClientWritable(sock, true);
                    return ServerResult::KeepConnection;
                }

                if (frame == RequestFrameStatus::UnsupportedTransferEncoding) {
                    s.responseBuf = makeResponse("HTTP/1.1 501 Not Implemented",
                        "text/plain; charset=utf-8",
                        "Unsupported Transfer-Encoding.\n", false);
                    s.responseView = s.responseBuf;
                    s.closeAfterSend = true;
                    setClientWritable(sock, true);
                    return ServerResult::KeepConnection;
                }

                if (frame == RequestFrameStatus::BadRequest
                    || frame == RequestFrameStatus::Complete) {
                    // Consume exactly one request so any pipelined tail remains
                    // queued for the next response cycle.
                    if (frame == RequestFrameStatus::BadRequest) {
                        consumed = s.request.size();
                    }
                    if (consumeRequestMessage_(s.request, consumed)
                        && consumed < s.request.size()) {
                        s.queuedRequest.append(s.request.data() + consumed,
                            s.request.size() - consumed);
                        s.request.resize(consumed);
                    }
                    s.requestScanPos = 0;
                    dispatchBuildResponse(s);
                    setClientWritable(sock, true);
                    return ServerResult::KeepConnection;
                }
            }
        } else if (n == 0) {
            return ServerResult::Disconnect;
        } else {
#ifdef AISOCKS_ENABLE_TLS
            if (isTlsMode(s) && s.tlsSession) {
                const int tlsErr = s.tlsSession->getLastErrorCode(n);
                if (tlsErr == SSL_ERROR_WANT_READ
                    || tlsErr == SSL_ERROR_WANT_WRITE)
                    break;
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

ServerResult HttpPollServer::onWritable(TcpSocket& sock, HttpClientState& s) {
    while (true) {
#ifdef AISOCKS_ENABLE_TLS
        if (isTlsMode(s) && !s.tlsHandshakeDone) {
            const ServerResult hs = doTlsHandshakeStep(sock, s);
            if (hs != ServerResult::KeepConnection) return hs;
            if (!s.tlsHandshakeDone) {
                setClientWritable(sock, s.tlsWantsWrite);
                return ServerResult::KeepConnection;
            }
            setClientWritable(sock, false);
        }
#endif

        if (s.responseView.empty()) return ServerResult::KeepConnection;

        if (!s.responseStarted && !s.interimResponse) {
            s.responseStarted = true;
            onResponseBegin(s);
        }

        int sent = tlsWrite(sock, s, s.responseView.data() + s.sent,
            s.responseView.size() - s.sent);

        if (sent > 0) {
            touchClient(sock, std::chrono::steady_clock::now());
            s.sent += static_cast<size_t>(sent);
        } else {
            const auto err = sock.getLastError();
            if (err == SocketError::WouldBlock || err == SocketError::Timeout)
                return ServerResult::KeepConnection;
            return ServerResult::Disconnect;
        }

        if (s.sent < s.responseView.size()) return ServerResult::KeepConnection;

        if (s.interimResponse) {
            s.responseView = {};
            s.responseBuf.clear();
            s.sent = 0;
            s.responseStarted = false;
            s.closeAfterSend = false;
            s.interimResponse = false;
            setClientWritable(sock, false);
            return ServerResult::KeepConnection;
        }

        onResponseSent(s);
        const bool shouldClose = s.closeAfterSend;
        resetAfterSend_(s);
        setClientWritable(sock, false);

        if (shouldClose) return ServerResult::Disconnect;

        if (!s.request.empty()
            && requestComplete(s.request, s.requestScanPos)) {
            // Immediately process queued pipelined data already buffered on
            // this connection.
            size_t consumed = 0;
            if (consumeRequestMessage_(s.request, consumed)
                && consumed < s.request.size()) {
                s.queuedRequest.append(
                    s.request.data() + consumed, s.request.size() - consumed);
                s.request.resize(consumed);
            }
            s.requestScanPos = 0;
            dispatchBuildResponse(s);
            setClientWritable(sock, true);
            continue;
        }

        return ServerResult::KeepConnection;
    }
}

void HttpPollServer::resetAfterSend_(HttpClientState& s) {
    s.request = std::move(s.queuedRequest);
    s.queuedRequest.clear();
    s.responseView = {};
    s.responseBuf.clear();
    s.parsedRequest = HttpRequest{};
    s.sent = 0;
    s.responseStarted = false;
    s.closeAfterSend = false;
    s.expectContinueSent = false;
    s.interimResponse = false;
    s.requestScanPos = s.request.size() >= 3 ? s.request.size() - 3 : 0;
    if (!s.request.empty()) s.startTime = std::chrono::steady_clock::now();
}

ServerResult HttpPollServer::onIdle() {
    tracker_.record(clientCount(), peakClientCount());
    if (accessLogger_) accessLogger_->flush();
    return ServerBase<HttpClientState>::onIdle();
}

} // namespace aiSocks
