// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Poll-driven HTTP/1.x server built on HttpPollServer.
// HttpPollServer handles all HTTP framing; this file only contains the
// application-level response logic.

#include "HttpPollServer.h"
#include <cstdio>
#include <cstdlib>

using namespace aiSocks;

class HttpServer : public HttpPollServer {
    private:
    std::string cached_response_;
    std::string cached_bad_request_;

    public:
    explicit HttpServer(const ServerBind& bind) : HttpPollServer(bind) {
        // Pre-build responses to avoid string concatenation overhead
        cached_response_ = "HTTP/1.1 200 OK\r\n"
                           "Content-Type: text/html; charset=utf-8\r\n"
                           "Content-Length: 92\r\n"
                           "\r\n"
                           "<html><body><h1>Hello World!</h1>"
                           "<p>This is a normal HTTP server response.</p>"
                           "</body></html>";

        cached_bad_request_
            = "HTTP/1.1 400 Bad Request\r\n"
              "Content-Type: text/plain; charset=utf-8\r\n"
              "Content-Length: 52\r\n"
              "\r\n"
              "Bad Request: this server only accepts HTTP requests.\n";

        setKeepAliveTimeout(std::chrono::seconds{5});
        printf("Listening on %s:%d\n", bind.address.c_str(),
            static_cast<int>(bind.port));
    }

    protected:
    void buildResponse(HttpClientState& s) override {
        bool keepAlive = !s.closeAfterSend;
        if (isHttpRequest(s.request)) {
            s.response = cached_response_;
            if (!keepAlive) {
                s.response += "Connection: close\r\n";
            }
        } else {
            s.response = cached_bad_request_;
            if (!keepAlive) {
                s.response += "Connection: close\r\n";
            }
        }
    }
};

int main() {
    printf("=== Poll-Driven HTTP Server ===\n");

    HttpServer server(ServerBind{"0.0.0.0", Port{8080}, 1024});
    if (!server.isValid()) {
        printf("Server failed to start\n");
        return 1;
    }

    server.run(ClientLimit::Unlimited, Milliseconds{0});
    printf("\nShutting down cleanly.\n");
    return 0;
}
