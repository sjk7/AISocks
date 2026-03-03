// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Example: SimpleClient one-liner HTTP client
// Demonstrates how to use SimpleClient for a quick GET request.

#include "../lib/include/SimpleClient.h"
#include <iostream>
#include <string>

using namespace aiSocks;

int main() {
    std::cout << "SimpleClient example: GET request to httpbin.org\n\n";

    SimpleClient client(ConnectArgs{"httpbin.org", Port{80}});
    if (!client.isConnected()) {
        std::cerr << "Connection failed: " << client.getLastError() << "\n";
        return 1;
    }

    client.run([](TcpSocket& sock) {
        const char* request = "GET /get?param=hello HTTP/1.0\r\n"
                              "Host: httpbin.org\r\n"
                              "Connection: close\r\n"
                              "\r\n";

        if (!sock.sendAll(request, strlen(request))) {
            std::cerr << "Failed to send request\n";
            return;
        }

        char buf[4096];
        int totalRead = 0;
        int n;
        while ((n = sock.receive(buf, sizeof(buf) - 1)) > 0) {
            buf[n] = '\0';
            std::cout.write(buf, n);
            totalRead += n;
        }
        std::cout << "\n\nTotal bytes read: " << totalRead << "\n";
    });

    return 0;
}
