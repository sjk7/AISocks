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
#include <sstream>

using namespace aiSocks;

// Configuration
constexpr size_t CHUNK_SIZE = 64 * 1024; // 64 KB chunks
constexpr size_t TOTAL_DATA = 100 * 1024 * 1024; // 100 MB total
constexpr uint16_t TEST_PORT = 18080;

// Structure to store transfer results
struct TransferResult {
    std::string label;
    double serverMBps;
    double clientMBps;
    bool success;
};

std::vector<TransferResult> results;
TransferResult currentResult;

// Helper function to check if an address is in a typical LAN range
bool isLikelyLanAddress(const std::string& address) {
    // Parse the address to check if it's in common LAN ranges
    std::istringstream iss(address);
    int a, b, c, d;
    char dot;
    if (!(iss >> a >> dot >> b >> dot >> c >> dot >> d)) {
        return false; // Invalid IP format
    }

    // Avoid APIPA addresses (169.254.x.x)
    if (a == 169 && b == 254) {
        return false;
    }

    // Check for common LAN ranges:
    // 192.168.0.0 - 192.168.255.255 (Class C private) - MOST COMMON for home
    // networks 10.0.0.0 - 10.255.255.255 (Class A private) 172.16.0.0 -
    // 172.31.255.255 (Class B private)
    if ((a == 192 && b == 168) || (a == 10)
        || (a == 172 && (b >= 16 && b <= 31))) {
        return true;
    }

    return false;
}

// Helper function to get priority score for address selection (higher = better)
int getAddressPriority(const std::string& address) {
    std::istringstream iss(address);
    int a, b, c, d;
    char dot;
    if (!(iss >> a >> dot >> b >> dot >> c >> dot >> d)) {
        return -1; // Invalid IP
    }

    // Avoid APIPA addresses
    if (a == 169 && b == 254) {
        return 0;
    }

    // Priority scoring:
    // 192.168.x.x = 3 (highest priority - most common home LAN)
    // 10.x.x.x = 2 (corporate LAN)
    // 172.16-31.x.x = 1 (corporate LAN)
    // Other non-loopback = 1 (fallback)

    if (a == 192 && b == 168) {
        return 3;
    } else if (a == 10) {
        return 2;
    } else if (a == 172 && (b >= 16 && b <= 31)) {
        return 1;
    } else {
        return 1; // Other valid non-loopback addresses
    }
}

void runServer(const std::string& bindAddr) {
    std::cout << "Starting server on " << bindAddr << ":" << TEST_PORT
              << "...\n";

    auto serverSocket = TcpSocket::createRaw();
    (void)serverSocket.setReuseAddress(true);

    if (!serverSocket.bind(bindAddr, Port{TEST_PORT})) {
        std::cerr << "Failed to bind to " << bindAddr << ":" << TEST_PORT
                  << ": " << serverSocket.getErrorMessage() << "\n";
        return;
    }
    if (!serverSocket.listen(5)) {
        std::cerr << "Failed to listen on " << bindAddr << ":" << TEST_PORT
                  << ": " << serverSocket.getErrorMessage() << "\n";
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

    // Store server speed
    currentResult.serverMBps = mb / seconds;

    clientSocket->close();
    serverSocket.close();
}

void runClient(const std::string& addr) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::cout << "Connecting to " << addr << ":" << TEST_PORT << "...\n";
    std::cout.flush();

    auto clientSocket = TcpSocket::createRaw();
    std::cout << "  [DEBUG] Attempting connect with 5s timeout...\n";
    std::cout.flush();
    if (!clientSocket.connect(addr, Port{TEST_PORT}, Milliseconds{5000})) {
        std::cerr << "Connect failed to " << addr << ":" << TEST_PORT << ": "
                  << clientSocket.getErrorMessage() << "\n";
        return;
    }
    std::cout << "  [DEBUG] Connect succeeded\n";
    std::cout.flush();

    std::cout << "Connected! Receiving...\n";

    std::vector<char> buffer(CHUNK_SIZE);
    size_t totalReceived = 0;
    auto startTime = std::chrono::high_resolution_clock::now();

    while (totalReceived < TOTAL_DATA) {
        int n = clientSocket.receive(buffer.data(), buffer.size());
        if (n <= 0) {
            std::cout << "  [DEBUG] receive returned " << n << ", error: "
                      << static_cast<int>(clientSocket.getLastError()) << "\n";
            std::cout.flush();
            break;
        }
        totalReceived += static_cast<size_t>(n);
        if (totalReceived % (10 * 1024 * 1024) == 0) {
            std::cout << "  [DEBUG] received "
                      << (totalReceived / (1024 * 1024)) << " MB so far\n";
            std::cout.flush();
        }
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

    // Store client speed
    currentResult.clientMBps = mb / seconds;
    currentResult.success = true;

    clientSocket.close();
}

void runTest(const std::string& label, const std::string& addr) {
    std::cout << "\n--- " << label << " (" << addr << ") ---\n";
    std::cout.flush();

    // Initialize current result
    currentResult = {label, 0.0, 0.0, false};

    std::thread serverThread(runServer, addr);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    runClient(addr);
    std::cout << "  [DEBUG] Waiting for server thread to finish...\n";
    std::cout.flush();
    if (serverThread.joinable()) {
        serverThread.join();
        std::cout << "  [DEBUG] Server thread finished\n";
    }

    // Store the result
    results.push_back(currentResult);
    std::cout.flush();
}

int main() {
    std::cout << "=== aiSocks Transfer Speed Test ===\n";
    std::cout << "Transfer: " << (TOTAL_DATA / (1024 * 1024)) << " MB  "
              << "Chunk: " << (CHUNK_SIZE / 1024) << " KB\n";

    // Always run loopback
    runTest("Loopback", "127.0.0.1");

    // Find best non-loopback IPv4 interface using priority scoring
    auto ifaces = Socket::getLocalAddresses();
    std::string bestAddress;
    int bestPriority = 0;

    for (const auto& iface : ifaces) {
        if (!iface.isLoopback && iface.family == AddressFamily::IPv4) {
            int priority = getAddressPriority(iface.address);
            if (priority > bestPriority) {
                bestPriority = priority;
                bestAddress = iface.address;
            }
        }
    }

    if (!bestAddress.empty()) {
        std::string addressType;
        if (bestPriority == 3) {
            addressType = "Home LAN (192.168.x.x)";
        } else if (bestPriority == 2) {
            addressType = "Corporate LAN (10.x.x.x)";
        } else if (bestPriority == 1) {
            addressType = "Other LAN";
        } else {
            addressType = "Non-LAN";
        }

        std::cout << "\nFound " << addressType << " interface: (" << bestAddress
                  << ")\n";
        runTest("Non-loopback", bestAddress);
    } else {
        std::cout
            << "\nNo suitable non-loopback IPv4 interface found; skipping.\n";
    }

    // Print summary in GB/s
    std::cout << "\n=== Transfer Speed Summary ===\n";
    std::cout << std::fixed << std::setprecision(3);
    for (const auto& result : results) {
        if (result.success) {
            std::cout << result.label << ":\n";
            std::cout << "  Server: " << (result.serverMBps / 1024.0)
                      << " GB/s\n";
            std::cout << "  Client: " << (result.clientMBps / 1024.0)
                      << " GB/s\n";
        } else {
            std::cout << result.label << ": Failed\n";
        }
    }

    std::cout << "\nDone.\n";
    return 0;
}
