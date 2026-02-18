// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
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
constexpr uint16_t TEST_PORT = 18080;

void runServer(const std::string& bindAddr) {
    std::cout << "Starting server on " << bindAddr << ":" << TEST_PORT
              << "...\n";

    auto serverSocket = TcpSocket::createRaw();
    serverSocket.setReuseAddress(true);

    if (!serverSocket.bind(bindAddr, Port{TEST_PORT})) {
        std::cerr << "Failed to bind: " << serverSocket.getErrorMessage()
                  << "\n";
        return;
    }
    if (!serverSocket.listen(5)) {
        std::cerr << "Failed to listen: " << serverSocket.getErrorMessage()
                  << "\n";
        return;
    }

    std::cout << "Server listening...\n";

    auto clientSocket = serverSocket.accept();
    if (!clientSocket) {
        std::cerr << "Failed to accept: " << serverSocket.getErrorMessage()
                  << "\n";
        return;
    }

    std::cout << "Client connected! Sending...\n";

    std::vector<char> buffer(CHUNK_SIZE, 'A');
    size_t totalSent = 0;
    auto startTime = std::chrono::high_resolution_clock::now();

    while (totalSent < TOTAL_DATA) {
        size_t toSend = std::min(CHUNK_SIZE, TOTAL_DATA - totalSent);
        int bytesSent = clientSocket->send(buffer.data(), toSend);
        if (bytesSent <= 0) {
            std::cerr << "Send failed: " << clientSocket->getErrorMessage()
                      << "\n";
            break;
        }
        totalSent += bytesSent;
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    double seconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                         endTime - startTime)
                         .count()
        / 1000.0;
    double mb = totalSent / (1024.0 * 1024.0);

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  [server] sent " << mb << " MB in " << seconds
              << "s = " << (mb / seconds) << " MB/s (" << (mb * 8 / seconds)
              << " Mbps)\n";

    clientSocket->close();
    serverSocket.close();
}

void runClient(const std::string& addr) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::cout << "Connecting to " << addr << ":" << TEST_PORT << "...\n";

    auto clientSocket = TcpSocket::createRaw();
    if (!clientSocket.connect(addr, Port{TEST_PORT})) {
        std::cerr << "Connect failed: " << clientSocket.getErrorMessage()
                  << "\n";
        return;
    }

    std::cout << "Connected! Receiving...\n";

    std::vector<char> buffer(CHUNK_SIZE);
    size_t totalReceived = 0;
    auto startTime = std::chrono::high_resolution_clock::now();

    while (totalReceived < TOTAL_DATA) {
        int n = clientSocket.receive(buffer.data(), buffer.size());
        if (n <= 0) break;
        totalReceived += static_cast<size_t>(n);
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    double seconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                         endTime - startTime)
                         .count()
        / 1000.0;
    double mb = totalReceived / (1024.0 * 1024.0);

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  [client] received " << mb << " MB in " << seconds
              << "s = " << (mb / seconds) << " MB/s (" << (mb * 8 / seconds)
              << " Mbps)\n";

    clientSocket.close();
}

void runTest(const std::string& label, const std::string& addr) {
    std::cout << "\n--- " << label << " (" << addr << ") ---\n";
    std::thread serverThread(runServer, addr);
    runClient(addr);
    serverThread.join();
}

int main() {
    std::cout << "=== aiSocks Transfer Speed Test ===\n";
    std::cout << "Transfer: " << (TOTAL_DATA / (1024 * 1024)) << " MB  "
              << "Chunk: " << (CHUNK_SIZE / 1024) << " KB\n";

    // Always run loopback
    runTest("Loopback", "127.0.0.1");

    // Find first non-loopback IPv4 interface
    auto ifaces = Socket::getLocalAddresses();
    std::string nonLoopback;
    for (const auto& iface : ifaces) {
        if (!iface.isLoopback && iface.family == AddressFamily::IPv4) {
            nonLoopback = iface.address;
            std::cout << "\nFound interface: " << iface.name << " ("
                      << iface.address << ")\n";
            break;
        }
    }

    if (!nonLoopback.empty()) {
        runTest("Non-loopback", nonLoopback);
    } else {
        std::cout << "\nNo non-loopback IPv4 interface found; skipping.\n";
    }

    std::cout << "\nDone.\n";
    return 0;
}
