// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Example: SimpleClient connecting to Google
// Demonstrates receiving data until connection closes

#include "SimpleClient.h"
#include <iostream>
#include <string>
#include <cstring>

using namespace aiSocks;

int main() {
    std::cout << "=== SimpleClient Google Connect Example ===\n";
    std::cout << "Connecting to google.com:80 and reading response...\n\n";

    unsigned long totalBytesRead = 0;
    
    // One-liner: connect to Google and read until closed
    SimpleClient client("google.com", Port{80}, [&](TcpSocket& sock) {
        // Send HTTP GET request
        const char* httpRequest = 
            "GET / HTTP/1.1\r\n"
            "Host: google.com\r\n"
            "Connection: close\r\n"
            "User-Agent: aiSocks-Example/1.0\r\n"
            "\r\n";

        std::cout << "Sending HTTP request...\n";
        if (!sock.sendAll(httpRequest, std::strlen(httpRequest))) {
            std::cerr << "Failed to send request\n";
            return;
        }

        // Read response data until connection closes (receive returns 0 or -1)
        char buffer[4096];
        int bytesRead;
        bool isFirstChunk = true;

        std::cout << "Reading response...\n";
        std::cout << "─────────────────────────────────────────\n\n";

        while ((bytesRead = sock.receive(buffer, sizeof(buffer) - 1)) > 0) {
            buffer[bytesRead] = '\0';
            
            // Print first 2KB to console, show stats for rest
            if (totalBytesRead < 2048) {
                int toPrint = std::min(static_cast<int>(sizeof(buffer) - 1 - totalBytesRead), 
                                       bytesRead);
                std::cout.write(buffer, toPrint);
                std::cout.flush();
            } else if (isFirstChunk && totalBytesRead > 0) {
                std::cout << "\n... (response truncated, showing stats) ...\n";
                isFirstChunk = false;
            }

            totalBytesRead += bytesRead;
        }

        std::cout << "\n─────────────────────────────────────────\n";
    });

    // Check connection result
    if (!client.isConnected()) {
        std::cerr << "Connection failed: " 
                  << static_cast<int>(client.getLastError()) << "\n";
        return 1;
    }

    std::cout << "\nConnection complete!\n";
    std::cout << "Total bytes received: " << totalBytesRead << "\n";

    return 0;
}
