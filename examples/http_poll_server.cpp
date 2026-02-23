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
public:
    explicit HttpServer(const ServerBind& bind) : HttpPollServer(bind) {
        printf("Listening on %s:%d\n", bind.address.c_str(), static_cast<int>(bind.port));
    }

protected:
    void buildResponse(HttpClientState& s) override {
        bool keepAlive = !s.closeAfterSend;
        if (isHttpRequest(s.request)) {
            s.response = makeResponse("HTTP/1.1 200 OK",
                "text/html; charset=utf-8",
                "<html><body><h1>Hello World!</h1>"
                "<p>This is a normal HTTP server response.</p>"
                "</body></html>",
                keepAlive);
        } else {
            s.response = makeResponse("HTTP/1.1 400 Bad Request",
                "text/plain; charset=utf-8",
                "Bad Request: this server only accepts HTTP requests.\n",
                keepAlive);
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
    
    server.run(ClientLimit::Unlimited, Milliseconds{1});
    printf("\nShutting down cleanly.\n");
    return 0;
}
