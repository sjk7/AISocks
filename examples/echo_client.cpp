// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
#include "SimpleClient.h"
#include <iostream>
#include <string>

using namespace aiSocks;

int main() {
    std::cout << "=== Echo Client Test ===\n";
    std::cout << "Connecting to localhost:8080\n\n";

    try {
        ConnectArgs args{
            .address = "127.0.0.1",
            .port = Port{8080},
            .connectTimeout = Milliseconds{1000}
        };

        SimpleClient client(args, [](TcpSocket& sock) {
            std::cout << "Connected to echo server!\n";

            // Test messages to send
            const char* messages[] = {
                "Hello, Echo Server!",
                "Testing 123",
                "How are you?"
            };

            for (const char* msg : messages) {
                std::cout << "\n[Sending] " << msg << "\n";
                
                // Send the message
                size_t len = std::strlen(msg);
                if (!sock.sendAll(msg, len)) {
                    std::cerr << "[Error] Failed to send: " 
                              << static_cast<int>(sock.getLastError()) << "\n";
                    break;
                }

                // Receive the echo
                char buffer[1024];
                int received = sock.receive(buffer, sizeof(buffer));
                
                if (received < 0) {
                    std::cerr << "[Error] Failed to receive: " 
                              << static_cast<int>(sock.getLastError()) << "\n";
                    break;
                }
                
                if (received == 0) {
                    std::cout << "[Info] Server closed connection\n";
                    break;
                }

                std::cout << "[Received] ";
                std::cout.write(buffer, received);
                std::cout << " (" << received << " bytes)\n";

                // Verify echo matches
                if (received == static_cast<int>(len) && 
                    std::memcmp(buffer, msg, len) == 0) {
                    std::cout << "[Success] Echo matches!\n";
                } else {
                    std::cout << "[Warning] Echo doesn't match sent data\n";
                }
            }

            std::cout << "\n[Done] All messages sent and echoed\n";
        });

    } catch (const SocketException& e) {
        std::cerr << "Connection failed: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
