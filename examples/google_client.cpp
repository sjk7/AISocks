// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Example: SimpleClient connecting to Google
// Demonstrates receiving data until connection closes

#include "SimpleClient.h"
#include <cstdio>
#include <string>
#include <cstring>

using namespace aiSocks;

// Connects to a server, sends an HTTP request, and receives the response
// Returns 0 on success, 1 on failure
int httpConnect(const ConnectArgs& args, const char* httpRequest) {
    size_t totalBytesRead = 0;

    SimpleClient client(args);
    if (!client.isConnected()) {
        fprintf(stderr, "\n*** CONNECTION FAILED ***\n");
        return 1;
    }

    client.run([&](TcpSocket& sock) {
        if (!sock.isBlocking()) {
            fprintf(stderr, "Expected a blocking socket\n"); //-V1056
            return;
        }
        printf("Connected! Socket is valid.\n");

        printf("Sending HTTP request...\n");
        if (!sock.sendAll(httpRequest, strlen(httpRequest))) {
            fprintf(stderr, "Failed to send request\n");
            return;
        }

        // Read response data until connection closes (receive returns 0 or -1)
        char buffer[4096];
        size_t bytesRead;
        bool isFirstChunk = true;
        int retval = 0;
        printf("Reading response...\n");
        printf("-----------------------------------------\n\n");

        while ((retval = (bytesRead
                    = sock.receive(buffer, sizeof(buffer) - 1))) //-V101 //-V103
            > 0) {
            buffer[bytesRead] = '\0';

            // Print first 2KB to console, show stats for rest
            if (totalBytesRead < 2048) {
                int toPrint = std::min(
                    static_cast<int>(sizeof(buffer) - 1 - totalBytesRead),
                    static_cast<int>(bytesRead)); //-V202
                fwrite(buffer, 1, static_cast<size_t>(toPrint), stdout);
                fflush(stdout);
            } else if (isFirstChunk) {
                printf("\n... (response truncated, showing stats) ...\n");
                isFirstChunk = false;
            }

            totalBytesRead += static_cast<size_t>(bytesRead);
        }

        if (totalBytesRead == 0 && retval < 0) {
            printf("\nConnection closed by server, after sending zero bytes\n");
            printf("Last error: %d - %s\n",
                static_cast<int>(sock.getLastError()),
                sock.getErrorMessage().c_str());
        }

        printf("\n-----------------------------------------\n");
    });

    printf("\nConnection complete!\n");
    printf("Total bytes received: %zu\n", totalBytesRead);
    return 0;
}

int main() {
    printf("=== SimpleClient Google Connect Example ===\n");
    printf("Connecting to google.com:8765 with 1s timeout...\n\n");

    // HTTP GET request
    const char* httpRequest = "GET / HTTP/1.1\r\n"
                              "Host: google.com\r\n"
                              "Connection: close\r\n"
                              "User-Agent: aiSocks-Example/1.0\r\n"
                              "\r\n";

    auto ret = httpConnect(
        ConnectArgs{"google.com", Port{80}, Milliseconds{1000}}, httpRequest);

    printf("8765 Example finished with code: %d\n", ret);

    httpConnect(
        ConnectArgs{"google.com", Port{8765}, Milliseconds{1000}}, httpRequest);
}
