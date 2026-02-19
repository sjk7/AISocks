// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Poll-driven HTTP/1.x server built on ServerBase.
// ServerBase owns all client socket/state bookkeeping; this file only
// contains HTTP framing logic.

#include "ServerBase.h"
#include <chrono>
#include <iostream>
#include <string>

using namespace aiSocks;

// ---------------------------------------------------------------------------
// HTTP helpers
// ---------------------------------------------------------------------------
namespace {

static constexpr size_t MAX_REQUEST_BYTES = 64 * 1024;

bool isHttpRequest(const std::string& req) {
    return req.rfind("GET ", 0) == 0 || req.rfind("POST", 0) == 0
        || req.rfind("PUT ", 0) == 0 || req.rfind("HEAD", 0) == 0
        || req.rfind("DELE", 0) == 0 || req.rfind("OPTI", 0) == 0
        || req.rfind("PATC", 0) == 0;
}

bool requestComplete(const std::string& req) {
    return req.find("\r\n\r\n") != std::string::npos;
}

std::string makeResponse(
    const char* statusLine, const char* contentType, const std::string& body) {
    std::string r;
    r.reserve(256 + body.size());
    r += statusLine;
    r += "\r\nContent-Type: ";
    r += contentType;
    r += "\r\nContent-Length: ";
    r += std::to_string(body.size());
    r += "\r\nConnection: close\r\n\r\n";
    r += body;
    return r;
}

} // namespace

// ---------------------------------------------------------------------------
// Per-connection state
// ---------------------------------------------------------------------------
using Clock = std::chrono::steady_clock;

static constexpr size_t PAYLOAD_BYTES = 100 * 1024 * 1024; // 100 MB

struct HttpClientState {
    std::string request;
    std::string response;
    size_t sent{0};
    Clock::time_point startTime{};
    bool timerStarted{false};
};

// ---------------------------------------------------------------------------
// HttpServer -- derives from ServerBase, handles HTTP framing only
// ---------------------------------------------------------------------------
class HttpServer : public ServerBase<HttpClientState> {
    public:
    explicit HttpServer(const ServerBind& bind)
        : ServerBase<HttpClientState>(bind) {
        std::cout << "Listening on " << bind.address << ":"
                  << static_cast<int>(bind.port) << "\n";
    }

    protected:
    bool onReadable(TcpSocket& sock, HttpClientState& s) override {
        char buf[4096];
        for (;;) {
            int n = sock.receive(buf, sizeof(buf));
            if (n > 0) {
                s.request.append(buf, static_cast<size_t>(n));

                if (s.request.size() > MAX_REQUEST_BYTES) {
                    s.response = makeResponse("HTTP/1.1 413 Payload Too Large",
                        "text/plain; charset=utf-8", "Request too large.\n");
                    return true; // let onWritable flush then disconnect
                }

                if (s.response.empty() && requestComplete(s.request)) {
                    buildResponse(s);
                    return true;
                }
            } else if (n == 0) {
                return false; // peer closed
            } else {
                const auto err = sock.getLastError();
                if (err == SocketError::WouldBlock
                    || err == SocketError::Timeout) {
                    break; // no more data right now
                }
                return false; // real error
            }
        }
        return true;
    }

    bool onWritable(TcpSocket& sock, HttpClientState& s) override {
        if (s.response.empty()) return true; // nothing to send yet

        if (!s.timerStarted) {
            s.startTime = Clock::now();
            s.timerStarted = true;
            std::cout << "Sending " << (PAYLOAD_BYTES / (1024 * 1024))
                      << " MB...\n";
        }

        while (s.sent < s.response.size()) {
            const char* out = s.response.data() + s.sent;
            const size_t left = s.response.size() - s.sent;
            int n = sock.send(out, left);
            if (n > 0) {
                s.sent += static_cast<size_t>(n);
            } else {
                const auto err = sock.getLastError();
                if (err == SocketError::WouldBlock
                    || err == SocketError::Timeout) {
                    break;
                }
                return false;
            }
        }

        if (s.sent >= s.response.size()) {
            const auto elapsed
                = std::chrono::duration<double>(Clock::now() - s.startTime)
                      .count();
            const double mb
                = static_cast<double>(PAYLOAD_BYTES) / (1024.0 * 1024.0);
            std::cout << "Sent " << mb << " MB in " << elapsed << " s  ("
                      << (mb / elapsed) << " MB/s)\n";
            sock.shutdown(ShutdownHow::Both);
            return false; // done -- remove client
        }
        return true;
    }

    private:
    static void buildResponse(HttpClientState& s) {
        if (isHttpRequest(s.request)) {
            // 100 MB of repeating 'A' bytes
            std::string body(PAYLOAD_BYTES, 'A');
            s.response = makeResponse(
                "HTTP/1.1 200 OK", "application/octet-stream", body);
        } else {
            s.response = makeResponse("HTTP/1.1 400 Bad Request",
                "text/plain; charset=utf-8",
                "Bad Request: this server only accepts HTTP requests.\n");
        }
    }
};

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    std::cout << "=== Poll-Driven HTTP Server ===\n";
    try {
        HttpServer server(ServerBind{
            .address = "0.0.0.0",
            .port = Port{8080},
            .backlog = 64,
        });
        server.run();
        std::cout << "\nShutting down cleanly.\n";
    } catch (const SocketException& e) {
        std::cerr << "Server error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
