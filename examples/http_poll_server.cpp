// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
#include "SimpleServer.h"

#include <cstring>
#include <iostream>
#include <string>
#include <unordered_map>

using namespace aiSocks;

namespace {

struct ClientState {
    std::string request;
    std::string response;
    size_t sent = 0;
};

bool isHttpRequest(const std::string& req) {
    return req.rfind("GET ", 0) == 0 || req.rfind("POST", 0) == 0
        || req.rfind("PUT ", 0) == 0 || req.rfind("HEAD", 0) == 0
        || req.rfind("DELE", 0) == 0 || req.rfind("OPTI", 0) == 0
        || req.rfind("PATC", 0) == 0;
}

bool requestComplete(const std::string& req) {
    return req.find("\r\n\r\n") != std::string::npos;
}

std::string makeHttpResponse(
    const char* statusLine, const char* contentType, const std::string& body) {
    std::string response;
    response.reserve(256 + body.size());
    response += statusLine;
    response += "\r\nContent-Type: ";
    response += contentType;
    response += "\r\nContent-Length: ";
    response += std::to_string(body.size());
    response += "\r\nConnection: close\r\n\r\n";
    response += body;
    return response;
}

} // namespace

int main() {
    std::cout << "=== Poll-Driven HTTP Server Example ===\n";
    std::cout << "Starting HTTP-only server on 0.0.0.0:8080\n";
    std::cout << "Accept/read/write are all poll-driven and non-blocking.\n\n";

    try {
        ServerBind config{
            .address = "0.0.0.0",
            .port = Port{8080},
            .backlog = 64,
        };

        SimpleServer server(config);
        std::unordered_map<const Socket*, ClientState> states;

        server.pollClients([&states](TcpSocket& client, PollEvent events) {
            const Socket* key = &client;
            auto& state = states[key];

            if (hasFlag(events, PollEvent::Readable)) {
                char buffer[4096];
                for (;;) {
                    int received = client.receive(buffer, sizeof(buffer));
                    if (received > 0) {
                        state.request.append(buffer, static_cast<size_t>(received));

                        if (state.request.size() > 64 * 1024) {
                            state.response = makeHttpResponse(
                                "HTTP/1.1 413 Payload Too Large",
                                "text/plain; charset=utf-8",
                                "Request too large.\n");
                            break;
                        }

                        if (state.response.empty() && requestComplete(state.request)) {
                            if (isHttpRequest(state.request)) {
                                const std::string body =
                                    "<!DOCTYPE html>\n"
                                    "<html><body>\n"
                                    "<h1>HTTP Poll Server</h1>\n"
                                    "<p>Server is running on port 8080.</p>\n"
                                    "<p>This endpoint is HTTP-only.</p>\n"
                                    "</body></html>\n";
                                state.response = makeHttpResponse(
                                    "HTTP/1.1 200 OK", "text/html; charset=utf-8", body);
                            } else {
                                state.response = makeHttpResponse(
                                    "HTTP/1.1 400 Bad Request",
                                    "text/plain; charset=utf-8",
                                    "Bad Request: this server only accepts HTTP requests.\n");
                            }
                            break;
                        }
                    } else if (received == 0) {
                        states.erase(key);
                        return false;
                    } else {
                        const auto err = client.getLastError();
                        if (err == SocketError::WouldBlock || err == SocketError::Timeout) {
                            break;
                        }
                        states.erase(key);
                        return false;
                    }
                }
            }

            if (hasFlag(events, PollEvent::Writable) && !state.response.empty()) {
                while (state.sent < state.response.size()) {
                    const char* out = state.response.data() + state.sent;
                    const size_t left = state.response.size() - state.sent;
                    int n = client.send(out, left);

                    if (n > 0) {
                        state.sent += static_cast<size_t>(n);
                    } else {
                        const auto err = client.getLastError();
                        if (err == SocketError::WouldBlock || err == SocketError::Timeout) {
                            break;
                        }
                        states.erase(key);
                        return false;
                    }
                }

                if (state.sent >= state.response.size()) {
                    client.shutdown(ShutdownHow::Both);
                    states.erase(key);
                    return false;
                }
            }

            return true;
        });

    } catch (const SocketException& e) {
        std::cerr << "Server error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
