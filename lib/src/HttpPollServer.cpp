// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it. PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#include "HttpPollServer.h"
#include "HttpProtocolDispatcher.h"

#include "BuildInfo.h"
#include "FileIO.h"
#include "HttpParserUtils.h"
#include "HttpRequestFramer.h"
#include "HttpRequest.h"
#include "HttpResponse.h"
#include <algorithm>
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
        detail::parseHeaderFields(headerSection, firstNL,
            [&](const std::string& key, std::string_view val) {
                if (key == "expect" && ciTokenPresent_(val, "100-continue"))
                    expect100Continue = true;
            });
        return expect100Continue;
    }

    static bool parseStatusLine_(std::string_view statusLine, int& codeOut,
        std::string_view& reasonOut) {
        const size_t firstSpace = statusLine.find(' ');
        if (firstSpace == std::string_view::npos) return false;
        const size_t codeStart = firstSpace + 1;
        if (codeStart >= statusLine.size()) return false;

        const size_t secondSpace = statusLine.find(' ', codeStart);
        const std::string_view codeStr = secondSpace == std::string_view::npos
            ? statusLine.substr(codeStart)
            : statusLine.substr(codeStart, secondSpace - codeStart);

        int code = 0;
        auto [ptr, ec] = std::from_chars(
            codeStr.data(), codeStr.data() + codeStr.size(), code);
        if (ec != std::errc{} || ptr != codeStr.data() + codeStr.size())
            return false;

        codeOut = code;
        reasonOut = secondSpace == std::string_view::npos
            ? std::string_view{}
            : statusLine.substr(secondSpace + 1);
        return true;
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
                [&](const std::string& key, std::string_view val) {
                    if (key == "transfer-encoding") {
                        if (!transferEncodingValue.empty())
                            transferEncodingValue.append(",");
                        transferEncodingValue.append(val.data(), val.size());
                        hasTransferEncoding = true;
                    } else if (key == "content-length") {
                        size_t parsed = 0;
                        bool overflow = false;
                        if (!detail::parseContentLengthWithLimit(
                                val, parsed, overflow, maxBodyBytes)) {
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
            if (!detail::transferEncodingEndsInChunked(transferEncodingValue))
                return RequestFrameStatus::UnsupportedTransferEncoding;

            size_t chunkedConsumed = 0;
            switch (detail::parseChunkedBodyWithLimit(
                body, maxBodyBytes, chunkedConsumed)) {
                case detail::ChunkedBodyParseResult::NeedMore:
                    return RequestFrameStatus::NeedMore;
                case detail::ChunkedBodyParseResult::BodyTooLarge:
                    return RequestFrameStatus::BodyTooLarge;
                case detail::ChunkedBodyParseResult::BadFrame:
                    return RequestFrameStatus::BadRequest;
                case detail::ChunkedBodyParseResult::Complete:
                    consumedLen = headerBytes + chunkedConsumed;
                    return RequestFrameStatus::Complete;
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
    // HttpPollServer relies on non-blocking I/O for its state-machine.
    // Ensure the listening socket is correctly configured before starting.
    if (getSocket().isBlocking()) {
        getSocket().setBlocking(false);
    }

#ifndef NDEBUG
    printf("[DEBUG] HttpPollServer::run() - Starting server\n");
    fflush(stdout);
#endif

    if (!protocolDispatcher_) {
        protocolDispatcher_ = std::make_unique<HttpProtocolDispatcher>(*this);
    }

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
    // Sockets in the poll server MUST remain non-blocking.
    // We enforce this here so even if a user manages to get hold of the
    // client socket, we keep it in the correct mode for the infrastructure.
    if (sock.isBlocking()) {
        sock.setBlocking(false);
    }

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
    const int statusCode = (s.responseStatusCode != 0)
        ? s.responseStatusCode
        : AccessLogger::extractStatusCode(s.dataView);
    const size_t bytesSent = (s.responseBytesPlanned != 0)
        ? s.responseBytesPlanned
        : s.dataView.size();
    accessLogger_->log(s.peerAddress, requestLine, statusCode, bytesSent);
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
    int code = 200;
    std::string_view reason = "OK";
    const std::string_view statusLineView
        = statusLine ? std::string_view(statusLine) : std::string_view{};
    if (!statusLine || !parseStatusLine_(statusLineView, code, reason)) {
        code = 500;
        reason = "Internal Server Error";
    }

    return HttpResponse::builder()
        .status(code, reason)
        .contentType(contentType ? contentType : "text/plain; charset=utf-8")
        .body(body)
        .keepAlive(keepAlive)
        .build();
}

void HttpPollServer::setStreamedFileResponse(HttpClientState& s,
    std::string headerBlock, std::shared_ptr<File> file, size_t fileSize) {
    s.dataBuf = std::move(headerBlock);
    s.dataView = s.dataBuf;
    s.sent = 0;

    s.streamingFile = std::move(file);
    s.streamingRemaining = fileSize;
    s.streamingBodyActive = (s.streamingFile != nullptr) && (fileSize > 0);

    s.responseStatusCode = AccessLogger::extractStatusCode(s.dataView);
    s.responseBytesPlanned = s.dataView.size() + fileSize;
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

// Headers are considered incomplete if we have some data but haven't
// reached a double-CRLF separator, OR if we are waiting for a TLS handshake.
bool HttpPollServer::slowlorisExpired(const HttpClientState& s,
    std::chrono::steady_clock::time_point now, int timeoutMs) const {
    if (!s.dataView.empty()) return false; // already responding

    bool handshakeDone = true;
#ifdef AISOCKS_ENABLE_TLS
    handshakeDone = s.tlsHandshakeDone;
#endif

    if (s.request.empty() && handshakeDone) return false;

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
            s.dataBuf = makeResponse("HTTP/1.1 405 Method Not Allowed",
                "text/plain; charset=utf-8",
                "405 Method Not Allowed\nOnly GET and HEAD are supported.\n",
                false);
            s.dataView = s.dataBuf;
            s.closeAfterSend = true;
            s.parsedRequest = HttpRequest{};
            return;
        }
    }

    auto req = HttpRequest::parse(s.request);
    if (!req.valid) {
        s.dataBuf = makeResponse("HTTP/1.1 400 Bad Request",
            "text/plain; charset=utf-8", "400 Bad Request\n", false);
        s.dataView = s.dataBuf;
        s.closeAfterSend = true;
        s.parsedRequest = HttpRequest{};
        return;
    }

    // Determine connection semantics and cache the parse result so
    // buildResponse() overrides do not need to re-parse the same bytes.
    if (req.version == "HTTP/1.1" && !req.header("host")) {
        s.dataBuf = makeResponse("HTTP/1.1 400 Bad Request",
            "text/plain; charset=utf-8",
            "400 Bad Request\nHost header required for HTTP/1.1.\n", false);
        s.dataView = s.dataBuf;
        s.closeAfterSend = true;
        s.parsedRequest = HttpRequest{};
        return;
    }

    if (const std::string* expect = req.header("expect")) {
        if (!ciTokenPresent_(*expect, "100-continue")) {
            s.dataBuf = makeResponse("HTTP/1.1 417 Expectation Failed",
                "text/plain; charset=utf-8", "417 Expectation Failed\n", false);
            s.dataView = s.dataBuf;
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
            s.dataBuf = makeResponse("HTTP/1.1 403 Forbidden",
                "text/plain; charset=utf-8", "403 Forbidden\nAccess denied.\n",
                false);
            s.dataView = s.dataBuf;
            s.closeAfterSend = true;
            s.parsedRequest = HttpRequest{};
            return;
        }
    }

    buildResponse(s);
}

ServerResult HttpPollServer::onReadable(TcpSocket& sock, HttpClientState& s) {
    if (protocolDispatcher_) {
        return protocolDispatcher_->onReadable(sock, s);
    }
    return ServerResult::Disconnect;
}

ServerResult HttpPollServer::onWritable(TcpSocket& sock, HttpClientState& s) {
    if (protocolDispatcher_) {
        return protocolDispatcher_->onWritable(sock, s);
    }
    return ServerResult::Disconnect;
}

bool HttpPollServer::runTlsHandshakeStage_(
    TcpSocket& sock, HttpClientState& s, ServerResult& out) {
#ifdef AISOCKS_ENABLE_TLS
    if (isTlsMode(s) && !s.tlsHandshakeDone) {
        const ServerResult hs = doTlsHandshakeStep(sock, s);
        if (hs != ServerResult::KeepConnection) {
            out = hs;
            return true;
        }
        if (!s.tlsHandshakeDone) {
            setClientWritable(sock, s.tlsWantsWrite);
            out = ServerResult::KeepConnection;
            return true;
        }
        setClientWritable(sock, false);
    }
#else
    (void)sock;
    (void)s;
#endif
    out = ServerResult::KeepConnection;
    return false;
}

bool HttpPollServer::runRequestFrameInspectionStage_(
    TcpSocket& sock, HttpClientState& s, ServerResult& out) {
    size_t consumed = 0;
    const RequestFrameStatus frame = inspectRequestFrame_(
        std::string_view(s.request), consumed, HttpPollServer::MAX_HEADER_SIZE);
    if (frame == RequestFrameStatus::NeedMore) {
        if (!s.expectContinueSent && shouldSend100Continue_(s.request, frame)) {
            s.dataBuf = "HTTP/1.1 100 Continue\r\n\r\n";
            s.dataView = s.dataBuf;
            s.sent = 0;
            s.responseStarted = false;
            s.closeAfterSend = false;
            s.interimResponse = true;
            s.expectContinueSent = true;
            setClientWritable(sock, true);
            out = ServerResult::KeepConnection;
            return true;
        }
        return false;
    }

    if (frame == RequestFrameStatus::HeaderTooLarge) {
        s.dataBuf = makeResponse("HTTP/1.1 431 Request Header Fields Too Large",
            "text/plain; charset=utf-8", "Header section too large.\n", false);
        s.dataView = s.dataBuf;
        s.closeAfterSend = true;
        setClientWritable(sock, true);
        out = ServerResult::KeepConnection;
        return true;
    }

    if (frame == RequestFrameStatus::BodyTooLarge) {
        s.dataBuf = makeResponse("HTTP/1.1 413 Payload Too Large",
            "text/plain; charset=utf-8", "Request body is too large.\n", false);
        s.dataView = s.dataBuf;
        s.closeAfterSend = true;
        setClientWritable(sock, true);
        out = ServerResult::KeepConnection;
        return true;
    }

    if (frame == RequestFrameStatus::UnsupportedTransferEncoding) {
        s.dataBuf = makeResponse("HTTP/1.1 501 Not Implemented",
            "text/plain; charset=utf-8", "Unsupported Transfer-Encoding.\n",
            false);
        s.dataView = s.dataBuf;
        s.closeAfterSend = true;
        setClientWritable(sock, true);
        out = ServerResult::KeepConnection;
        return true;
    }

    if (frame == RequestFrameStatus::BadRequest
        || frame == RequestFrameStatus::Complete) {
        // Consume exactly one request so any pipelined tail remains queued
        // for the next response cycle.
        if (frame == RequestFrameStatus::BadRequest) {
            consumed = s.request.size();
        } else {
            consumeRequestMessage_(s.request, consumed);
        }

        if (consumed < s.request.size()) {
            s.queuedRequest.append(
                s.request.data() + consumed, s.request.size() - consumed);
            s.request.resize(consumed);
        }
        s.requestScanPos = 0;
        dispatchBuildResponse(s);
        setClientWritable(sock, true);
        out = ServerResult::KeepConnection;
        return true;
    }

    return false;
}

bool HttpPollServer::runSendStreamStage_(
    TcpSocket& sock, HttpClientState& s, ServerResult& out) {
    if (!s.responseStarted && !s.interimResponse) {
        s.responseStarted = true;
        if (s.responseStatusCode == 0)
            s.responseStatusCode = AccessLogger::extractStatusCode(s.dataView);
        if (s.responseBytesPlanned == 0)
            s.responseBytesPlanned = s.dataView.size();
        onResponseBegin(s);
    }

    int sent = tlsWrite(
        sock, s, s.dataView.data() + s.sent, s.dataView.size() - s.sent);

    if (sent > 0) {
        touchClient(sock, std::chrono::steady_clock::now());
        s.sent += static_cast<size_t>(sent);
    } else {
#ifdef AISOCKS_ENABLE_TLS
        if (isTlsMode(s) && s.tlsSession) {
            const int tlsErr = s.tlsSession->getLastErrorCode(sent);
            if (tlsErr == SSL_ERROR_WANT_READ
                || tlsErr == SSL_ERROR_WANT_WRITE) {
                setClientWritable(sock, tlsErr == SSL_ERROR_WANT_WRITE);
                out = ServerResult::KeepConnection;
                return true;
            }
        }
#endif
        const auto err = sock.getLastError();
        if (err == SocketError::WouldBlock || err == SocketError::Timeout) {
            out = ServerResult::KeepConnection;
            return true;
        }
        out = ServerResult::Disconnect;
        return true;
    }

    if (s.sent < s.dataView.size()) {
        out = ServerResult::KeepConnection;
        return true;
    }

    if (s.streamingBodyActive) {
        if (!s.streamingFile || !s.streamingFile->isOpen()) {
            out = ServerResult::Disconnect;
            return true;
        }

        if (s.streamingRemaining == 0) {
            s.streamingBodyActive = false;
            s.streamingFile.reset();
        } else {
            const size_t toRead
                = std::min(s.streamingChunkBuf.size(), s.streamingRemaining);
            const size_t got
                = s.streamingFile->read(s.streamingChunkBuf.data(), 1, toRead);
            if (got == 0) {
                out = ServerResult::Disconnect;
                return true;
            }

            s.streamingRemaining -= got;
            s.dataView = std::string_view(s.streamingChunkBuf.data(), got);
            s.sent = 0;

            if (s.streamingRemaining == 0) {
                s.streamingBodyActive = false;
                s.streamingFile.reset();
            }
            return false;
        }
    }

    if (s.interimResponse) {
        s.dataView = {};
        s.dataBuf.clear();
        s.sent = 0;
        s.responseStarted = false;
        s.closeAfterSend = false;
        s.interimResponse = false;
        setClientWritable(sock, false);
        out = ServerResult::KeepConnection;
        return true;
    }

    onResponseSent(s);
    const bool shouldClose = s.closeAfterSend;
    resetAfterSend_(s);
    setClientWritable(sock, false);
    if (shouldClose) {
        out = ServerResult::Disconnect;
        return true;
    }

    return false;
}

bool HttpPollServer::runPipelineContinuationStage_(
    TcpSocket& sock, HttpClientState& s, ServerResult& out) {
    if (!s.request.empty() && requestComplete(s.request, s.requestScanPos)) {
        // Immediately process queued pipelined data already buffered on this
        // connection.
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
        return false;
    }

    out = ServerResult::KeepConnection;
    return true;
}

void HttpPollServer::resetAfterSend_(HttpClientState& s) {
    s.request = std::move(s.queuedRequest);
    s.queuedRequest.clear();
    s.dataView = {};
    s.dataBuf.clear();
    s.streamingBodyActive = false;
    s.streamingFile.reset();
    s.streamingRemaining = 0;
    s.parsedRequest = HttpRequest{};
    s.sent = 0;
    s.responseStatusCode = 0;
    s.responseBytesPlanned = 0;
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

    // Periodically flush TLS errors (e.g., every 10 seconds under load)
    const auto now = std::chrono::steady_clock::now();
    if (suppressedTlsErrors_ > 0
        && std::chrono::duration_cast<std::chrono::seconds>(
               now - lastTlsErrorFlush_)
                .count()
            >= 10) {
        flushTlsErrors();
    }

    return ServerBase<HttpClientState>::onIdle();
}

void HttpPollServer::recordTlsHandshakeSuppressed() {
    suppressedTlsErrors_++;
}

void HttpPollServer::flushTlsErrors() {
    if (suppressedTlsErrors_ > 0) {
        if (accessLogger_) {
            // Internal log entry for suppressed handshake errors
            accessLogger_->logHandshakeSuppression(suppressedTlsErrors_);
        }
        std::fprintf(stderr,
            "[tls] handshake failed sslErr=unexpected eof sslCode=1 [%llu]\n",
            static_cast<unsigned long long>(suppressedTlsErrors_));
        suppressedTlsErrors_ = 0;
    }
    lastTlsErrorFlush_ = std::chrono::steady_clock::now();
}

} // namespace aiSocks
