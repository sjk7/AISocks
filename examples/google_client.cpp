// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Example: SimpleClient connecting to Google
// Demonstrates receiving data until connection closes

#include "SimpleClient.h"
#include <iostream>
#include <string>
#include <cstring>

using namespace aiSocks;

// Connects to a server, sends an HTTP request, and receives the response
// Returns 0 on success, 1 on failure
int httpConnect(const ConnectArgs& args, const char* httpRequest) {
    size_t totalBytesRead = 0;

    SimpleClient client(args);
    if (!client.isConnected()) {
        std::cerr << "\n*** CONNECTION FAILED ***\n";
        return 1;
    }

    client.run([&](TcpSocket& sock) {
        if (!sock.isBlocking()) {
            std::cerr << "Expected a blocking socket\n"; //-V1056
            return;
        }
        std::cout << "Connected! Socket is valid.\n";

        std::cout << "Sending HTTP request...\n";
        if (!sock.sendAll(httpRequest, strlen(httpRequest))) {
            std::cerr << "Failed to send request\n";
            return;
        }

        // Read response data until connection closes (receive returns 0 or -1)
        char buffer[4096];
        size_t bytesRead;
        bool isFirstChunk = true;
        int retval = 0;
        std::cout << "Reading response...\n";
        std::cout << "-----------------------------------------\n\n";

        while ((retval = (bytesRead = sock.receive(buffer, sizeof(buffer) - 1)))
            > 0) {
            buffer[bytesRead] = '\0';

            // Print first 2KB to console, show stats for rest
            if (totalBytesRead < 2048) {
                int toPrint = std::min(
                    static_cast<int>(sizeof(buffer) - 1 - totalBytesRead),
                    static_cast<int>(bytesRead));
                std::cout.write(buffer, toPrint);
                std::cout.flush();
            } else if (isFirstChunk) {
                std::cout << "\n... (response truncated, showing stats) ...\n";
                isFirstChunk = false;
            }

            totalBytesRead += static_cast<size_t>(bytesRead);
        }

        if (totalBytesRead == 0 && retval < 0) {
            std::cout
                << "\nConnection closed by server, after sending zero bytes\n";
            std::cout << "Last error: " << static_cast<int>(sock.getLastError())
                      << " - " << sock.getErrorMessage() << "\n";
        }

        std::cout << "\n-----------------------------------------\n";
    });

    std::cout << "\nConnection complete!\n";
    std::cout << "Total bytes received: " << totalBytesRead << "\n";
    return 0;
}

int main() {
    std::cout << "=== SimpleClient Google Connect Example ===\n";
    std::cout << "Connecting to google.com:8765 with 1s timeout...\n\n";

    // HTTP GET request
    const char* httpRequest = "GET / HTTP/1.1\r\n"
                              "Host: google.com\r\n"
                              "Connection: close\r\n"
                              "User-Agent: aiSocks-Example/1.0\r\n"
                              "\r\n";

    auto ret = httpConnect(
        ConnectArgs{"google.com", Port{80}, Milliseconds{1000}}, httpRequest);

    std::cout << "8765 Example finished with code: " << ret << "\n";

    httpConnect(
        ConnectArgs{"google.com", Port{8765}, Milliseconds{1000}}, httpRequest);
}
