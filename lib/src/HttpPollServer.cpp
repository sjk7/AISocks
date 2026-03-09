// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it. PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#include "HttpPollServer.h"

#include "BuildInfo.h"
#include "HttpRequest.h"
#include <cstdio>
#include <string>

namespace aiSocks {

void HttpPollServer::run(ClientLimit maxClients, Milliseconds timeout) {
    if (!this->isValid()) {
        printf("ERROR: Server failed to start - port %d is already in use "
               "or invalid\n",
            bind_.port.value());
        return;
    }

#ifndef NDEBUG
    printf("[DEBUG] HttpPollServer::run() - Starting server\n");
    fflush(stdout);
#endif

    ServerBase<HttpClientState>::run(maxClients, timeout);

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
    return req.rfind("GET ", 0) == 0 || req.rfind("POST", 0) == 0
        || req.rfind("PUT ", 0) == 0 || req.rfind("HEAD", 0) == 0
        || req.rfind("DELE", 0) == 0 || req.rfind("OPTI", 0) == 0
        || req.rfind("PATC", 0) == 0;
}

bool HttpPollServer::requestComplete(const std::string& req) {
    return req.find("\r\n\r\n") != std::string::npos;
}

bool HttpPollServer::requestComplete(const std::string& req, size_t& scanPos) {
    const size_t start = scanPos >= 3 ? scanPos - 3 : 0;
    const size_t pos = req.find("\r\n\r\n", start);
    if (pos != std::string::npos) {
        return true;
    }
    scanPos = req.size() >= 3 ? req.size() - 3 : 0;
    return false;
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
    const bool hasKeepAlive = conn && (*conn == "keep-alive");
    const bool hasClose = conn && (*conn == "close");
    return http10 ? !hasKeepAlive : hasClose;
}

// Returns true when the slowloris deadline has expired: more than 5 seconds
// have elapsed since the first byte arrived but headers are still incomplete.
static bool isSlowlorisTimeout_(const HttpClientState& s) {
    if (!s.responseView.empty()) return false; // already responding
    if (s.request.empty()) return false;
    const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - s.startTime);
    return elapsed.count() > 5;
}

void HttpPollServer::dispatchBuildResponse(HttpClientState& s) {
    // Only allow GET and HEAD methods for security.
    if (s.request.compare(0, 4, "GET ") != 0
        && s.request.compare(0, 5, "HEAD ") != 0) {
        s.responseBuf = makeResponse("HTTP/1.1 405 Method Not Allowed",
            "text/plain; charset=utf-8",
            "405 Method Not Allowed\nOnly GET and HEAD are supported.\n",
            false);
        s.responseView = s.responseBuf;
        s.closeAfterSend = true;
        return;
    }

    // Parse once to determine connection semantics — avoids 7 serial
    // full-buffer scans and is immune to false matches inside the body.
    const auto req = HttpRequest::parse(s.request);
    s.closeAfterSend = resolveKeepAlive_(req);
    buildResponse(s);
}

ServerResult HttpPollServer::onReadable(TcpSocket& sock, HttpClientState& s) {
    char buf[RECV_BUF_SIZE];
    for (;;) {
        // Slowloris protection: drop if headers not received within 5 seconds.
        if (isSlowlorisTimeout_(s)) return ServerResult::Disconnect;

        int n = sock.receive(buf, sizeof(buf));

        if (n > 0) {
            touchClient(sock);
            s.request.append(buf, static_cast<size_t>(n));

            if (s.request.size() > MAX_REQUEST_BYTES) {
                s.responseBuf = makeResponse("HTTP/1.1 413 Payload Too Large",
                    "text/plain; charset=utf-8", "Request too large.\n", false);
                s.responseView = s.responseBuf;
                s.closeAfterSend = true;
                setClientWritable(sock, true);
                return onWritable(sock, s);
            }

            if (s.responseView.empty()
                && requestComplete(s.request, s.requestScanPos)) {
                dispatchBuildResponse(s);
                setClientWritable(sock, true);
                return onWritable(sock, s);
            }
        } else if (n == 0) {
            return ServerResult::Disconnect;
        } else {
            const auto err = sock.getLastError();
            if (err == SocketError::WouldBlock || err == SocketError::Timeout)
                break;
            return ServerResult::Disconnect;
        }
    }
    return ServerResult::KeepConnection;
}

ServerResult HttpPollServer::onWritable(TcpSocket& sock, HttpClientState& s) {
    if (s.responseView.empty()) return ServerResult::KeepConnection;

    if (!s.responseStarted) {
        s.responseStarted = true;
        onResponseBegin(s);
    }

    int sent = sock.sendChunked(
        s.responseView.data() + s.sent, s.responseView.size() - s.sent);

    if (sent > 0) {
        touchClient(sock);
        s.sent += static_cast<size_t>(sent);
    } else {
        const auto err = sock.getLastError();
        if (err == SocketError::WouldBlock || err == SocketError::Timeout)
            return ServerResult::KeepConnection;
        return ServerResult::Disconnect;
    }

    if (s.sent >= s.responseView.size()) {
        onResponseSent(s);
        const bool shouldClose = s.closeAfterSend;
        resetAfterSend_(s);
        setClientWritable(sock, false);
        return shouldClose ? ServerResult::Disconnect
                           : ServerResult::KeepConnection;
    }
    return ServerResult::KeepConnection;
}

void HttpPollServer::resetAfterSend_(HttpClientState& s) {
    s.request.clear();
    s.responseView = {};
    s.responseBuf.clear();
    s.sent = 0;
    s.responseStarted = false;
    s.closeAfterSend = false;
    s.requestScanPos = 0;
}

ServerResult HttpPollServer::onIdle() {
    tracker_.record(clientCount(), peakClientCount());
    return ServerBase<HttpClientState>::onIdle();
}

} // namespace aiSocks
