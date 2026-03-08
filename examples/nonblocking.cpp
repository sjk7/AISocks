// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#include "TcpSocket.h"
#include <cstdio>
#include <thread>
#include <chrono>
#include <string>
#include <cstring>
#include <vector>
#include <cstring>

using namespace aiSocks;

// Configuration
constexpr size_t CHUNK_SIZE = 64 * 1024; // 64 KB chunks
constexpr size_t TOTAL_DATA = 100 * 1024 * 1024; // 100 MB total

void runServerNonBlocking() {
    printf("Starting non-blocking server on port 8080...\n");

    auto serverSocket = TcpSocket::createRaw();

    if (!serverSocket.isValid()) {
        fprintf(stderr, "Failed to create server socket: %s\n",
            serverSocket.getErrorMessage().c_str());
        return;
    }

    (void)serverSocket.setReuseAddress(true);

    if (!serverSocket.bind("0.0.0.0", Port{8080})) {
        fprintf(stderr, "Failed to bind: %s\n",
            serverSocket.getErrorMessage().c_str());
        return;
    }

    if (!serverSocket.listen(5)) {
        fprintf(stderr, "Failed to listen: %s\n",
            serverSocket.getErrorMessage().c_str());
        return;
    }

    printf("Server listening on port 8080...\n");

    // Accept connection (blocking accept is fine)
    auto clientSocket = serverSocket.accept();
    if (!clientSocket) {
        fprintf(stderr, "Failed to accept connection: %s\n",
            serverSocket.getErrorMessage().c_str());
        return;
    }

    // Set client socket to non-blocking mode
    if (!clientSocket->setBlocking(false)) {
        fprintf(stderr, "Failed to set non-blocking mode: %s\n",
            clientSocket->getErrorMessage().c_str());
        return;
    }

    printf("Client connected! Starting non-blocking data transfer...\n");

    // Prepare data buffer
    std::vector<char> buffer(CHUNK_SIZE, 'A');
    size_t totalSent = 0;
    size_t bufferOffset = 0;
    size_t currentChunkSize = CHUNK_SIZE;

    auto startTime = std::chrono::high_resolution_clock::now();
    size_t wouldBlockCount = 0;
    size_t sendCount = 0;

    // Send data in non-blocking mode
    while (totalSent < TOTAL_DATA) {
        // Calculate how much to send in this chunk
        if (bufferOffset == 0) {
            currentChunkSize = std::min(CHUNK_SIZE, TOTAL_DATA - totalSent);
        }

        int bytesSent = clientSocket->send(
            buffer.data() + bufferOffset, currentChunkSize - bufferOffset);

        if (bytesSent > 0) {
            bufferOffset += static_cast<size_t>(bytesSent);
            sendCount++;

            // If we sent the whole chunk, reset for next chunk
            if (bufferOffset >= currentChunkSize) {
                totalSent += static_cast<size_t>(bufferOffset);
                bufferOffset = 0;
            }
        } else {
            // Check if it would block
            if (clientSocket->getLastError() == SocketError::WouldBlock) {
                wouldBlockCount++;
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            } else {
                fprintf(stderr, "Failed to send data: %s\n",
                    clientSocket->getErrorMessage().c_str());
                break;
            }
        }
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        endTime - startTime);

    double seconds = static_cast<double>(duration.count()) / 1000.0;
    double megabytes = static_cast<double>(totalSent) / (1024.0 * 1024.0);
    double speedMBps = megabytes / seconds;
    double speedMbps = speedMBps * 8;

    printf("\nServer Statistics (Non-blocking):\n");
    printf("  Total sent: %.2f MB\n", megabytes);
    printf("  Time: %.2f seconds\n", seconds);
    printf("  Speed: %.2f MB/s (%.2f Mbps)\n", speedMBps, speedMbps);
    printf("  Send calls: %zu\n", sendCount);
    printf("  Would-block events: %zu\n", wouldBlockCount);

    clientSocket->close();
    serverSocket.close();
}

void runClientNonBlocking() {
    // Give server time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    printf("Connecting to server...\n");

    auto clientSocket = TcpSocket::createRaw();

    if (!clientSocket.isValid()) {
        fprintf(stderr, "Failed to create client socket: %s\n",
            clientSocket.getErrorMessage().c_str());
        return;
    }

    // Connect (blocking connect is fine)
    if (!clientSocket.connect("127.0.0.1", Port{8080})) {
        fprintf(stderr, "Failed to connect: %s\n",
            clientSocket.getErrorMessage().c_str());
        return;
    }

    // Set socket to non-blocking mode
    if (!clientSocket.setBlocking(false)) {
        fprintf(stderr, "Failed to set non-blocking mode: %s\n",
            clientSocket.getErrorMessage().c_str());
        return;
    }

    printf("Connected to server! Starting non-blocking data transfer...\n");

    // Prepare receive buffer
    std::vector<char> buffer(CHUNK_SIZE);
    size_t totalReceived = 0;
    size_t wouldBlockCount = 0;
    size_t recvCount = 0;

    auto startTime = std::chrono::high_resolution_clock::now();

    // Receive data in non-blocking mode
    while (totalReceived < TOTAL_DATA) {
        int bytesReceived = clientSocket.receive(buffer.data(), buffer.size());

        if (bytesReceived > 0) {
            totalReceived += static_cast<size_t>(bytesReceived); //-V201
            recvCount++;
        } else if (bytesReceived == 0) {
            printf("Server closed connection\n");
            break;
        } else {
            // Check if it would block
            if (clientSocket.getLastError() == SocketError::WouldBlock) {
                wouldBlockCount++;
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            } else {
                fprintf(stderr, "Failed to receive data: %s\n",
                    clientSocket.getErrorMessage().c_str());
                break;
            }
        }
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        endTime - startTime);

    double seconds = static_cast<double>(duration.count()) / 1000.0;
    double megabytes = static_cast<double>(totalReceived) / (1024.0 * 1024.0);
    double speedMBps = megabytes / seconds;
    double speedMbps = speedMBps * 8;

    printf("\nClient Statistics (Non-blocking):\n");
    printf("  Total received: %.2f MB\n", megabytes);
    printf("  Time: %.2f seconds\n", seconds);
    printf("  Speed: %.2f MB/s (%.2f Mbps)\n", speedMBps, speedMbps);
    printf("  Receive calls: %zu\n", recvCount);
    printf("  Would-block events: %zu\n", wouldBlockCount);

    clientSocket.close();
}

int main() {
    printf("=== aiSocks Library - Non-Blocking I/O Speed Test ===\n");
    printf("Transfer size: %zu MB\n", TOTAL_DATA / (1024 * 1024));
    printf("Chunk size: %zu KB\n", CHUNK_SIZE / 1024);
    printf("\n");

    // Run server in a separate thread
    std::thread serverThread(runServerNonBlocking);

    // Run client in main thread
    runClientNonBlocking();

    // Wait for server to finish
    serverThread.join();

    printf("\n");
    printf("Non-blocking transfer test completed!\n");

    return 0;
}
