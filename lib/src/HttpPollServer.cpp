
#include "HttpPollServer.h"

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

    ServerBase<HttpClientState>::run(maxClients, timeout);

    printf("\nServer stopped gracefully.\n");
}

void HttpPollServer::printBuildInfo() {
    printf("Built: %s %s  |  OS: %s  |  Build: %s\n", __DATE__, __TIME__,
        buildOS(), buildKind());
}

void HttpPollServer::printStartupBanner() {
    printf("Built: %s %s  |  OS: %s  |  Build: %s\n", __DATE__, __TIME__,
        buildOS(), buildKind());
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
    r += std::to_string(body.size());
    r += keepAlive ? "\r\nConnection: keep-alive\r\n\r\n"
                   : "\r\nConnection: close\r\n\r\n";
    r += body;
    return r;
}

void HttpPollServer::dispatchBuildResponse(HttpClientState& s) {
    bool http10 = s.request.find("HTTP/1.0") != std::string::npos;
    bool hasKeepAlive
        = s.request.find("Connection: keep-alive") != std::string::npos
        || s.request.find("connection: keep-alive") != std::string::npos
        || s.request.find("Connection: Keep-Alive") != std::string::npos;
    bool hasClose = s.request.find("Connection: close") != std::string::npos
        || s.request.find("connection: close") != std::string::npos
        || s.request.find("Connection: Close") != std::string::npos;
    s.closeAfterSend = http10 ? !hasKeepAlive : hasClose;
    buildResponse(s);
}

ServerResult HttpPollServer::onReadable(TcpSocket& sock, HttpClientState& s) {
    char buf[RECV_BUF_SIZE];
    for (;;) {
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
        bool shouldClose = s.closeAfterSend;
        s.request.clear();
        s.responseView = {};
        s.responseBuf.clear();
        s.sent = 0;
        s.responseStarted = false;
        s.closeAfterSend = false;
        s.requestScanPos = 0;
        setClientWritable(sock, false);
        return shouldClose ? ServerResult::Disconnect
                           : ServerResult::KeepConnection;
    }
    return ServerResult::KeepConnection;
}

ServerResult HttpPollServer::onIdle() {
    auto now = std::chrono::steady_clock::now();
    auto interval
        = std::chrono::duration<double, std::milli>(now - last_call_).count();
    last_call_ = now;
    intervals_.push_back(interval);
    ++call_count_;

    auto since_print = std::chrono::duration<double>(now - last_print_).count();
    double print_interval = first_output_done_ ? 60.0 : 0.5;

    if (since_print >= print_interval) {
        if (!intervals_.empty()) {
            double sum = 0;
            for (double v : intervals_) sum += v;
            printf("onIdle() called %d times, avg interval: %.1fms  "
                   "clients: %zu  peak: %zu\n",
                call_count_, sum / static_cast<double>(intervals_.size()),
                static_cast<size_t>(clientCount()),
                static_cast<size_t>(peakClientCount()));
        }
        intervals_.clear();
        call_count_ = 0;
        last_print_ = now;
        first_output_done_ = true;
    }

    return ServerBase<HttpClientState>::onIdle();
}

} // namespace aiSocks
