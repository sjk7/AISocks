#include "Socket.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include <cstring>
#include <vector>
#include <iomanip>

using namespace aiSocks;

// Configuration
constexpr size_t CHUNK_SIZE = 64 * 1024;  // 64 KB chunks
constexpr size_t TOTAL_DATA = 100 * 1024 * 1024;  // 100 MB total

void runServer() {
    std::cout << "Starting server on port 8080..." << std::endl;
    
    Socket serverSocket(SocketType::TCP);
    
    if (!serverSocket.isValid()) {
        std::cerr << "Failed to create server socket: " 
                  << serverSocket.getErrorMessage() << std::endl;
        return;
    }
    
    serverSocket.setReuseAddress(true);
    
    if (!serverSocket.bind("0.0.0.0", 8080)) {
        std::cerr << "Failed to bind: " << serverSocket.getErrorMessage() << std::endl;
        return;
    }
    
    if (!serverSocket.listen(5)) {
        std::cerr << "Failed to listen: " << serverSocket.getErrorMessage() << std::endl;
        return;
    }
    
    std::cout << "Server listening on port 8080..." << std::endl;
    
    auto clientSocket = serverSocket.accept();
    if (!clientSocket) {
        std::cerr << "Failed to accept connection: " 
                  << serverSocket.getErrorMessage() << std::endl;
        return;
    }
    
    std::cout << "Client connected! Starting data transfer..." << std::endl;
    
    // Prepare data buffer
    std::vector<char> buffer(CHUNK_SIZE, 'A');
    size_t totalSent = 0;
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Send data in chunks
    while (totalSent < TOTAL_DATA) {
        size_t toSend = std::min(CHUNK_SIZE, TOTAL_DATA - totalSent);
        int bytesSent = clientSocket->send(buffer.data(), toSend);
        
        if (bytesSent <= 0) {
            std::cerr << "Failed to send data: " 
                      << clientSocket->getErrorMessage() << std::endl;
            break;
        }
        
        totalSent += bytesSent;
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    double seconds = duration.count() / 1000.0;
    double megabytes = totalSent / (1024.0 * 1024.0);
    double speedMBps = megabytes / seconds;
    double speedMbps = speedMBps * 8;
    
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\nServer Statistics:" << std::endl;
    std::cout << "  Total sent: " << megabytes << " MB" << std::endl;
    std::cout << "  Time: " << seconds << " seconds" << std::endl;
    std::cout << "  Speed: " << speedMBps << " MB/s (" << speedMbps << " Mbps)" << std::endl;
    
    clientSocket->close();
    serverSocket.close();
}

void runClient() {
    // Give server time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    std::cout << "Connecting to server..." << std::endl;
    
    Socket clientSocket(SocketType::TCP);
    
    if (!clientSocket.isValid()) {
        std::cerr << "Failed to create client socket: " 
                  << clientSocket.getErrorMessage() << std::endl;
        return;
    }
    
    if (!clientSocket.connect("127.0.0.1", 8080)) {
        std::cerr << "Failed to connect: " << clientSocket.getErrorMessage() << std::endl;
        return;
    }
    
    std::cout << "Connected to server! Starting data transfer..." << std::endl;
    
    // Prepare receive buffer
    std::vector<char> buffer(CHUNK_SIZE);
    size_t totalReceived = 0;
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Receive data until complete
    while (totalReceived < TOTAL_DATA) {
        int bytesReceived = clientSocket.receive(buffer.data(), buffer.size());
        
        if (bytesReceived <= 0) {
            if (bytesReceived == 0) {
                std::cout << "Server closed connection" << std::endl;
            } else {
                std::cerr << "Failed to receive data: " 
                          << clientSocket.getErrorMessage() << std::endl;
            }
            break;
        }
        
        totalReceived += bytesReceived;
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    double seconds = duration.count() / 1000.0;
    double megabytes = totalReceived / (1024.0 * 1024.0);
    double speedMBps = megabytes / seconds;
    double speedMbps = speedMBps * 8;
    
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\nClient Statistics:" << std::endl;
    std::cout << "  Total received: " << megabytes << " MB" << std::endl;
    std::cout << "  Time: " << seconds << " seconds" << std::endl;
    std::cout << "  Speed: " << speedMBps << " MB/s (" << speedMbps << " Mbps)" << std::endl;
    
    clientSocket.close();
}

int main() {
    std::cout << "=== aiSocks Library - Transfer Speed Test ===" << std::endl;
    std::cout << "Transfer size: " << (TOTAL_DATA / (1024 * 1024)) << " MB" << std::endl;
    std::cout << "Chunk size: " << (CHUNK_SIZE / 1024) << " KB" << std::endl;
    std::cout << std::endl;
    
    // Run server in a separate thread
    std::thread serverThread(runServer);
    
    // Run client in main thread
    runClient();
    
    // Wait for server to finish
    serverThread.join();
    
    std::cout << std::endl;
    std::cout << "Transfer test completed!" << std::endl;
    
    return 0;
}
