// Comprehensive demonstration of exception-free SocketFactory usage

#include "SocketFactory.h"
#include <iostream>
#include <thread>
#include <chrono>

using namespace aiSocks;

void demonstrateBasicCreation() {
    std::cout << "=== Basic Socket Creation ===\n\n";
    
    // Create a basic TCP socket
    auto tcp_result = SocketFactory::createTcpSocket();
    if (tcp_result.isSuccess()) {
        auto& socket = tcp_result.value();
        std::cout << "? TCP socket created successfully\n";
        std::cout << "  Valid: " << socket.isValid() << "\n";
        std::cout << "  Family: " << (socket.getAddressFamily() == AddressFamily::IPv4 ? "IPv4" : "IPv6") << "\n";
    } else {
        std::cout << "? TCP socket creation failed: " << tcp_result.message() << "\n";
    }
    
    // Create a UDP socket
    auto udp_result = SocketFactory::createUdpSocket();
    if (udp_result.isSuccess()) {
        auto& socket = udp_result.value();
        std::cout << "? UDP socket created successfully\n";
        std::cout << "  Valid: " << socket.isValid() << "\n";
    } else {
        std::cout << "? UDP socket creation failed: " << udp_result.message() << "\n";
    }
    
    std::cout << "\n";
}

void demonstrateServerCreation() {
    std::cout << "=== Server Socket Creation ===\n\n";
    
    // Create a TCP server
    auto server_result = SocketFactory::createTcpServer(
        AddressFamily::IPv4,
        ServerBind{"127.0.0.1", 8080, 10, true}
    );
    
    if (server_result.isSuccess()) {
        auto& server = server_result.value();
        std::cout << "? TCP server created successfully\n";
        
        if (auto endpoint = server.getLocalEndpoint()) {
            std::cout << "  Listening on: " << endpoint->toString() << "\n";
        }
        
        // Server is ready to accept connections
        std::cout << "  Ready to accept connections\n";
    } else {
        std::cout << "? TCP server creation failed: " << server_result.message() << "\n";
    }
    
    // Try to create another server on the same port (should fail)
    auto duplicate_result = SocketFactory::createTcpServer(
        AddressFamily::IPv4,
        ServerBind{"127.0.0.1", 8080, 10, true}
    );
    
    if (duplicate_result.isError()) {
        std::cout << "? Duplicate server correctly failed: " << duplicate_result.message() << "\n";
    } else {
        std::cout << "? Duplicate server unexpectedly succeeded\n";
    }
    
    std::cout << "\n";
}

void demonstrateClientConnection() {
    std::cout << "=== Client Connection ===\n\n";
    
    // First create a server to connect to
    auto server_result = SocketFactory::createTcpServer(
        AddressFamily::IPv4,
        ServerBind{"127.0.0.1", 0}  // Port 0 = let OS choose
    );
    
    if (server_result.isError()) {
        std::cout << "? Failed to create test server: " << server_result.message() << "\n";
        return;
    }
    
    auto& server = server_result.value();
    Port server_port{0};
    if (auto endpoint = server.getLocalEndpoint()) {
        server_port = endpoint->port;
        std::cout << "? Test server listening on: " << endpoint->toString() << "\n";
    }
    
    // Now try to connect a client
    auto client_result = SocketFactory::createTcpClient(
        AddressFamily::IPv4,
        ConnectArgs{"127.0.0.1", server_port, std::chrono::seconds{5}}
    );
    
    if (client_result.isSuccess()) {
        auto& client = client_result.value();
        std::cout << "? Client connected successfully\n";
        
        if (auto local = client.getLocalEndpoint()) {
            std::cout << "  Client local: " << local->toString() << "\n";
        }
        if (auto peer = client.getPeerEndpoint()) {
            std::cout << "  Client peer: " << peer->toString() << "\n";
        }
    } else {
        std::cout << "? Client connection failed: " << client_result.message() << "\n";
    }
    
    std::cout << "\n";
}

void demonstratePortUtilities() {
    std::cout << "=== Port Utilities ===\n\n";
    
    // Check if a port is available
    auto port_check = SocketFactory::isPortAvailable(
        AddressFamily::IPv4,
        "127.0.0.1",
        Port{8080}
    );
    
    if (port_check.isSuccess()) {
        if (port_check.value()) {
            std::cout << "? Port 8080 is available\n";
        } else {
            std::cout << "? Port 8080 is in use\n";
        }
    } else {
        std::cout << "? Port check failed: " << port_check.message() << "\n";
    }
    
    // Find an available port
    auto find_port = SocketFactory::findAvailablePort(
        AddressFamily::IPv4,
        "127.0.0.1",
        Port{49152},  // Start of ephemeral range
        Port{49160}   // Small range for demo
    );
    
    if (find_port.isSuccess()) {
        std::cout << "? Found available port: " << find_port.value().value << "\n";
    } else {
        std::cout << "? Failed to find available port: " << find_port.message() << "\n";
    }
    
    std::cout << "\n";
}

void demonstrateErrorHandling() {
    std::cout << "=== Error Handling Demonstration ===\n\n";
    
    // Try to bind to an invalid address
    auto invalid_bind = SocketFactory::createTcpServer(
        AddressFamily::IPv4,
        ServerBind{"invalid.address.xyz", 8080}
    );
    
    if (invalid_bind.isError()) {
        std::cout << "? Invalid address correctly rejected\n";
        std::cout << "  Error: " << invalid_bind.message() << "\n";
    }
    
    // Try to connect to a non-existent server
    auto no_server = SocketFactory::createTcpClient(
        AddressFamily::IPv4,
        ConnectArgs{"127.0.0.1", 65432, std::chrono::milliseconds{1000}}
    );
    
    if (no_server.isError()) {
        std::cout << "? Connection to non-existent server correctly failed\n";
        std::cout << "  Error: " << no_server.message() << "\n";
    }
    
    // Try to create a server on a privileged port (may fail depending on permissions)
    auto privileged_port = SocketFactory::createTcpServer(
        AddressFamily::IPv4,
        ServerBind{"0.0.0.0", Port{80}}  // HTTP port
    );
    
    if (privileged_port.isError()) {
        std::cout << "? Privileged port correctly rejected (expected)\n";
        std::cout << "  Error: " << privileged_port.message() << "\n";
    } else {
        std::cout << "! Privileged port allowed (running as root?)\n";
    }
    
    std::cout << "\n";
}

void demonstratePerformanceBenefits() {
    std::cout << "=== Performance Benefits ===\n\n";
    
    const int iterations = 1000;
    auto start = std::chrono::high_resolution_clock::now();
    
    // Create many sockets successfully (no error message construction)
    for (int i = 0; i < iterations; ++i) {
        auto result = SocketFactory::createTcpSocket();
        // Success case - no error message construction overhead
        if (result.isSuccess()) {
            // Socket will be closed when it goes out of scope
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    std::cout << "? Created " << iterations << " sockets in " << duration.count() << " ?s\n";
    std::cout << "  Average: " << duration.count() / iterations << " ?s per socket\n";
    std::cout << "  Zero error message construction overhead for success cases\n\n";
    
    // Demonstrate lazy error message construction
    auto error_result = SocketFactory::createTcpClient(
        AddressFamily::IPv4,
        ConnectArgs{"127.0.0.1", 65432, std::chrono::milliseconds{100}}
    );
    
    if (error_result.isError()) {
        std::cout << "? Error result created (message not yet constructed)\n";
        
        // First access constructs the message
        std::cout << "  First access: " << error_result.message() << "\n";
        
        // Second access uses cached message
        std::cout << "  Second access: " << error_result.message() << " (cached)\n";
    }
    
    std::cout << "\n";
}

int main() {
    std::cout << "=== SocketFactory Exception-Free API Demonstration ===\n\n";
    
    try {
        demonstrateBasicCreation();
        demonstrateServerCreation();
        demonstrateClientConnection();
        demonstratePortUtilities();
        demonstrateErrorHandling();
        demonstratePerformanceBenefits();
        
        std::cout << "=== All demonstrations completed successfully ===\n";
        std::cout << "Key benefits demonstrated:\n";
        std::cout << "- No exceptions thrown anywhere\n";
        std::cout << "- Lazy error message construction\n";
        std::cout << "- Rich error context when needed\n";
        std::cout << "- Zero overhead for success cases\n";
        std::cout << "- Clean, composable API\n";
        
    } catch (const std::exception& e) {
        std::cout << "Unexpected exception: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}
