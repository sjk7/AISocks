// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Poll-driven HTTP/1.x server built on HttpPollServer.
// HttpPollServer handles all HTTP framing; this file only contains the
// application-level response logic.

#include "HttpPollServer.h"
#include <iostream>

using namespace aiSocks;

class HttpServer : public HttpPollServer {
public:
    explicit HttpServer(const ServerBind& bind) : HttpPollServer(bind) {
        std::cout << "Listening on " << bind.address << ":"
                  << static_cast<int>(bind.port) << "\n";
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
    std::cout << "=== Poll-Driven HTTP Server ===\n";
    try {
        HttpServer server(ServerBind{
            .address = "0.0.0.0",
            .port    = Port{8080},
            .backlog = 1024,
        });
        server.run(0, Milliseconds{1});
        std::cout << "\nShutting down cleanly.\n";
    } catch (const SocketException& e) {
        std::cerr << "Server error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
