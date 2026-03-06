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
    printf("Starting server on %s:%u...\n", bindAddr.c_str(), TEST_PORT);

    auto serverSocket = TcpSocket::createRaw();
    (void)serverSocket.setReuseAddress(true);

    if (!serverSocket.bind(bindAddr, Port{TEST_PORT})) {
        fprintf(stderr, "Failed to bind to %s:%u: %s\n",
                bindAddr.c_str(), TEST_PORT,
                serverSocket.getErrorMessage().c_str());
        return;
    }
    if (!serverSocket.listen(5)) {
        fprintf(stderr, "Failed to listen on %s:%u: %s\n",
                bindAddr.c_str(), TEST_PORT,
                serverSocket.getErrorMessage().c_str());
        return;
    }

    printf("Server listening...\n");

    auto clientSocket = serverSocket.accept();
    if (!clientSocket) {
        fprintf(stderr, "Failed to accept: %s\n",
                serverSocket.getErrorMessage().c_str());
        return;
    }

    printf("Client connected! Sending...\n");

    std::vector<char> buffer(CHUNK_SIZE, 'A');
    size_t totalSent = 0;
    auto startTime = std::chrono::high_resolution_clock::now();

    while (totalSent < TOTAL_DATA) {
        size_t toSend = std::min(CHUNK_SIZE, TOTAL_DATA - totalSent);
        int bytesSent = clientSocket->send(buffer.data(), toSend);
        if (bytesSent <= 0) {
            fprintf(stderr, "Send failed: %s\n",
                    clientSocket->getErrorMessage().c_str());
            break;
        }
        totalSent += static_cast<size_t>(bytesSent); //-V201
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    double seconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                         endTime - startTime)
                         .count()
        / 1000.0;
    double mb = static_cast<double>(totalSent) / (1024.0 * 1024.0);

    printf("  [server] sent %.2f MB in %.2fs = %.2f MB/s (%.2f Mbps)\n",
           mb, seconds, mb / seconds, mb * 8 / seconds);

    // Store server speed
    currentResult.serverMBps = mb / seconds;

    clientSocket->close();
    serverSocket.close();
}

void runClient(const std::string& addr) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    printf("Connecting to %s:%u...\n", addr.c_str(), TEST_PORT);
    fflush(stdout);

    auto clientSocket = TcpSocket::createRaw();
    printf("  [DEBUG] Attempting connect with 5s timeout...\n");
    fflush(stdout);
    if (!clientSocket.connect(addr, Port{TEST_PORT}, Milliseconds{5000})) {
        fprintf(stderr, "Connect failed to %s:%u: %s\n",
                addr.c_str(), TEST_PORT,
                clientSocket.getErrorMessage().c_str());
        return;
    }
    printf("  [DEBUG] Connect succeeded\n");
    fflush(stdout);

    printf("Connected! Receiving...\n");

    std::vector<char> buffer(CHUNK_SIZE);
    size_t totalReceived = 0;
    auto startTime = std::chrono::high_resolution_clock::now();

    while (totalReceived < TOTAL_DATA) {
        size_t n = clientSocket.receive(buffer.data(), buffer.size()); //-V101
        if (n <= 0) {
            printf("  [DEBUG] receive returned %zu, error: %d\n",
                   n, static_cast<int>(clientSocket.getLastError()));
            fflush(stdout);
            break;
        }
        totalReceived += static_cast<size_t>(n); //-V201
        if (totalReceived % (10 * 1024 * 1024) == 0) {
            printf("  [DEBUG] received %zu MB so far\n",
                   totalReceived / (1024 * 1024));
            fflush(stdout);
        }
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    double seconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                         endTime - startTime)
                         .count()
        / 1000.0;
    double mb = static_cast<double>(totalReceived) / (1024.0 * 1024.0);

    printf("  [client] received %.2f MB in %.2fs = %.2f MB/s (%.2f Mbps)\n",
           mb, seconds, mb / seconds, mb * 8 / seconds);

    // Store client speed
    currentResult.clientMBps = mb / seconds;
    currentResult.success = true;

    clientSocket.close();
}

void runTest(const std::string& label, const std::string& addr) {
    printf("\n--- %s (%s) ---\n", label.c_str(), addr.c_str());
    fflush(stdout);

    // Initialize current result
    currentResult = {label, 0.0, 0.0, false};

    std::thread serverThread(runServer, addr);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    runClient(addr);
    printf("  [DEBUG] Waiting for server thread to finish...\n");
    fflush(stdout);
    if (serverThread.joinable()) {
        serverThread.join();
        printf("  [DEBUG] Server thread finished\n");
    }

    // Store the result
    results.push_back(currentResult);
    fflush(stdout);
}

int main() {
    printf("=== aiSocks Transfer Speed Test ===\n");
    printf("Transfer: %zu MB  Chunk: %zu KB\n",
           TOTAL_DATA / (1024 * 1024), CHUNK_SIZE / 1024);

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

        printf("\nFound %s interface: (%s)\n", addressType.c_str(), bestAddress.c_str());
        runTest("Non-loopback", bestAddress);
    } else {
        printf("\nNo suitable non-loopback IPv4 interface found; skipping.\n");
    }

    // Print summary in GB/s
    printf("\n=== Transfer Speed Summary ===\n");
    for (const auto& result : results) {
        if (result.success) {
            printf("%s:\n", result.label.c_str());
            printf("  Server: %.3f GB/s\n", result.serverMBps / 1024.0);
            printf("  Client: %.3f GB/s\n", result.clientMBps / 1024.0);
        } else {
            printf("%s: Failed\n", result.label.c_str());
        }
    }

    printf("\nDone.\n");
    return 0;
}
