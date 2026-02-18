// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
#include "TcpSocket.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include <cstring>
#include <vector>
#include <iomanip>

using namespace aiSocks;

// Configuration
constexpr size_t CHUNK_SIZE = 64 * 1024; // 64 KB chunks
constexpr size_t TOTAL_DATA = 100 * 1024 * 1024; // 100 MB total
constexpr int POLL_DELAY_MS = 1; // Polling delay

void runServerNonBlocking() {
    std::cout << "Starting non-blocking server on port 8080..." << std::endl;

    TcpSocket serverSocket;

    if (!serverSocket.isValid()) {
        std::cerr << "Failed to create server socket: "
                  << serverSocket.getErrorMessage() << std::endl;
        return;
    }

    serverSocket.setReuseAddress(true);

    if (!serverSocket.bind("0.0.0.0", Port::Known::HTTP_ALT)) {
        std::cerr << "Failed to bind: " << serverSocket.getErrorMessage()
                  << std::endl;
        return;
    }

    if (!serverSocket.listen(5)) {
        std::cerr << "Failed to listen: " << serverSocket.getErrorMessage()
                  << std::endl;
        return;
    }

    std::cout << "Server listening on port 8080..." << std::endl;

    // Accept connection (blocking accept is fine)
    auto clientSocket = serverSocket.accept();
    if (!clientSocket) {
        std::cerr << "Failed to accept connection: "
                  << serverSocket.getErrorMessage() << std::endl;
        return;
    }

    // Set client socket to non-blocking mode
    if (!clientSocket->setBlocking(false)) {
        std::cerr << "Failed to set non-blocking mode: "
                  << clientSocket->getErrorMessage() << std::endl;
        return;
    }

    std::cout << "Client connected! Starting non-blocking data transfer..."
              << std::endl;

    // Prepare data buffer
    std::vector<char> buffer(CHUNK_SIZE, 'A');
    size_t totalSent = 0;
    size_t bufferOffset = 0;
    size_t currentChunkSize = CHUNK_SIZE;

    auto startTime = std::chrono::high_resolution_clock::now();
    int wouldBlockCount = 0;
    int sendCount = 0;

    // Send data in non-blocking mode
    while (totalSent < TOTAL_DATA) {
        // Calculate how much to send in this chunk
        if (bufferOffset == 0) {
            currentChunkSize = std::min(CHUNK_SIZE, TOTAL_DATA - totalSent);
        }

        int bytesSent = clientSocket->send(
            buffer.data() + bufferOffset, currentChunkSize - bufferOffset);

        if (bytesSent > 0) {
            bufferOffset += bytesSent;
            sendCount++;

            // If we sent the whole chunk, reset for next chunk
            if (bufferOffset >= currentChunkSize) {
                totalSent += bufferOffset;
                bufferOffset = 0;
            }
        } else {
            // Check if it would block
            if (clientSocket->getLastError() == SocketError::WouldBlock) {
                wouldBlockCount++;
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            } else {
                std::cerr << "Failed to send data: "
                          << clientSocket->getErrorMessage() << std::endl;
                break;
            }
        }
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        endTime - startTime);

    double seconds = duration.count() / 1000.0;
    double megabytes = totalSent / (1024.0 * 1024.0);
    double speedMBps = megabytes / seconds;
    double speedMbps = speedMBps * 8;

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\nServer Statistics (Non-blocking):" << std::endl;
    std::cout << "  Total sent: " << megabytes << " MB" << std::endl;
    std::cout << "  Time: " << seconds << " seconds" << std::endl;
    std::cout << "  Speed: " << speedMBps << " MB/s (" << speedMbps << " Mbps)"
              << std::endl;
    std::cout << "  Send calls: " << sendCount << std::endl;
    std::cout << "  Would-block events: " << wouldBlockCount << std::endl;

    clientSocket->close();
    serverSocket.close();
}

void runClientNonBlocking() {
    // Give server time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    std::cout << "Connecting to server..." << std::endl;

    TcpSocket clientSocket;

    if (!clientSocket.isValid()) {
        std::cerr << "Failed to create client socket: "
                  << clientSocket.getErrorMessage() << std::endl;
        return;
    }

    // Connect (blocking connect is fine)
    if (!clientSocket.connect("127.0.0.1", Port::Known::HTTP_ALT)) {
        std::cerr << "Failed to connect: " << clientSocket.getErrorMessage()
                  << std::endl;
        return;
    }

    // Set socket to non-blocking mode
    if (!clientSocket.setBlocking(false)) {
        std::cerr << "Failed to set non-blocking mode: "
                  << clientSocket.getErrorMessage() << std::endl;
        return;
    }

    std::cout << "Connected to server! Starting non-blocking data transfer..."
              << std::endl;

    // Prepare receive buffer
    std::vector<char> buffer(CHUNK_SIZE);
    size_t totalReceived = 0;
    int wouldBlockCount = 0;
    int recvCount = 0;

    auto startTime = std::chrono::high_resolution_clock::now();

    // Receive data in non-blocking mode
    while (totalReceived < TOTAL_DATA) {
        int bytesReceived = clientSocket.receive(buffer.data(), buffer.size());

        if (bytesReceived > 0) {
            totalReceived += bytesReceived;
            recvCount++;
        } else if (bytesReceived == 0) {
            std::cout << "Server closed connection" << std::endl;
            break;
        } else {
            // Check if it would block
            if (clientSocket.getLastError() == SocketError::WouldBlock) {
                wouldBlockCount++;
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            } else {
                std::cerr << "Failed to receive data: "
                          << clientSocket.getErrorMessage() << std::endl;
                break;
            }
        }
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        endTime - startTime);

    double seconds = duration.count() / 1000.0;
    double megabytes = totalReceived / (1024.0 * 1024.0);
    double speedMBps = megabytes / seconds;
    double speedMbps = speedMBps * 8;

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\nClient Statistics (Non-blocking):" << std::endl;
    std::cout << "  Total received: " << megabytes << " MB" << std::endl;
    std::cout << "  Time: " << seconds << " seconds" << std::endl;
    std::cout << "  Speed: " << speedMBps << " MB/s (" << speedMbps << " Mbps)"
              << std::endl;
    std::cout << "  Receive calls: " << recvCount << std::endl;
    std::cout << "  Would-block events: " << wouldBlockCount << std::endl;

    clientSocket.close();
}

int main() {
    std::cout << "=== aiSocks Library - Non-Blocking I/O Speed Test ==="
              << std::endl;
    std::cout << "Transfer size: " << (TOTAL_DATA / (1024 * 1024)) << " MB"
              << std::endl;
    std::cout << "Chunk size: " << (CHUNK_SIZE / 1024) << " KB" << std::endl;
    std::cout << std::endl;

    // Run server in a separate thread
    std::thread serverThread(runServerNonBlocking);

    // Run client in main thread
    runClientNonBlocking();

    // Wait for server to finish
    serverThread.join();

    std::cout << std::endl;
    std::cout << "Non-blocking transfer test completed!" << std::endl;

    return 0;
}
