// Example showing how Result<T> would integrate with Socket APIs

#include "Result.h"
#include "Socket.h"
#include "TcpSocket.h"
#include <iostream>

using namespace aiSocks;

// Example of how TcpSocket could use Result<T>
namespace aiSocks {

class TcpSocketResult {
public:
    // Static factory methods that return Result instead of throwing
    static Result<TcpSocket> createServer(AddressFamily family, const ServerBind& config) {
        try {
            // This would internally use the non-throwing constructor
            auto socket = TcpSocket::createRaw(family);
            if (!socket.isValid()) {
                return Result<TcpSocket>::failure(
                    socket.getLastError(), 
                    "socket()", 
                    0, 
                    false
                );
            }
            
            // Set reuse address if requested
            if (config.reuseAddr && !socket.setReuseAddress(true)) {
                return Result<TcpSocket>::failure(
                    socket.getLastError(),
                    "setsockopt(SO_REUSEADDR)",
                    0,
                    false
                );
            }
            
            // Bind
            if (!socket.doBind(config.address, config.port)) {
                return Result<TcpSocket>::failure(
                    socket.getLastError(),
                    ("bind(" + config.address + ":" + std::to_string(config.port) + ")").c_str(),
                    0,
                    false
                );
            }
            
            // Listen
            if (!socket.doListen(config.backlog)) {
                return Result<TcpSocket>::failure(
                    socket.getLastError(),
                    ("listen(backlog=" + std::to_string(config.backlog) + ")").c_str(),
                    0,
                    false
                );
            }
            
            return Result<TcpSocket>::success(std::move(socket));
            
        } catch (const std::exception& e) {
            // This should never happen with the new design, but as a fallback
            return Result<TcpSocket>::failure(
                SocketError::Unknown,
                "TcpSocket::createServer",
                0,
                false
            );
        }
    }
    
    static Result<TcpSocket> connect(AddressFamily family, const ConnectArgs& config) {
        auto socket = TcpSocket::createRaw(family);
        if (!socket.isValid()) {
            return Result<TcpSocket>::failure(
                socket.getLastError(),
                "socket()",
                0,
                false
            );
        }
        
        // Connect with timeout
        if (!socket.doConnect(config.address, config.port, config.connectTimeout)) {
            return Result<TcpSocket>::failure(
                socket.getLastError(),
                ("connect(" + config.address + ":" + std::to_string(config.port) + ")").c_str(),
                0,
                false
            );
        }
        
        return Result<TcpSocket>::success(std::move(socket));
    }
};

} // namespace aiSocks

// Usage examples
void demonstrateResultApi() {
    std::cout << "=== Result<T> API Usage Examples ===\n\n";
    
    // Example 1: Server creation
    std::cout << "1. Creating a server:\n";
    auto server_result = TcpSocketResult::createServer(
        AddressFamily::IPv4, 
        ServerBind{"127.0.0.1", 8080}
    );
    
    if (server_result.isSuccess()) {
        auto& server = server_result.value();
        std::cout << "   ? Server created successfully\n";
        std::cout << "   Socket valid: " << server.isValid() << "\n";
    } else {
        std::cout << "   ? Server creation failed\n";
        std::cout << "   Error: " << server_result.message() << "\n";
        // Note: Error message is only constructed here because we accessed it!
    }
    
    std::cout << "\n";
    
    // Example 2: Client connection
    std::cout << "2. Connecting to a server:\n";
    auto client_result = TcpSocketResult::connect(
        AddressFamily::IPv4,
        ConnectArgs{"127.0.0.1", 8080}
    );
    
    if (client_result.isSuccess()) {
        auto& client = client_result.value();
        std::cout << "   ? Connected successfully\n";
        std::cout << "   Local endpoint: ";
        if (auto local = client.getLocalEndpoint()) {
            std::cout << local->toString() << "\n";
        } else {
            std::cout << "unknown\n";
        }
    } else {
        std::cout << "   ? Connection failed\n";
        std::cout << "   Error: " << client_result.message() << "\n";
        // Again, error message constructed only when needed
    }
    
    std::cout << "\n";
    
    // Example 3: Chaining operations
    std::cout << "3. Chaining operations:\n";
    
    auto create_and_bind = []() -> Result<TcpSocket> {
        auto socket = TcpSocket::createRaw(AddressFamily::IPv4);
        if (!socket.isValid()) {
            return Result<TcpSocket>::failure(
                socket.getLastError(),
                "socket()",
                0,
                false
            );
        }
        
        if (!socket.setReuseAddress(true)) {
            return Result<TcpSocket>::failure(
                socket.getLastError(),
                "setsockopt(SO_REUSEADDR)",
                0,
                false
            );
        }
        
        if (!socket.doBind("0.0.0.0", 0)) {  // Let OS choose port
            return Result<TcpSocket>::failure(
                socket.getLastError(),
                "bind(0.0.0.0:0)",
                0,
                false
            );
        }
        
        return Result<TcpSocket>::success(std::move(socket));
    };
    
    auto chained_result = create_and_bind();
    if (chained_result.isSuccess()) {
        auto& sock = chained_result.value();
        if (auto endpoint = sock.getLocalEndpoint()) {
            std::cout << "   ? Bound to: " << endpoint->toString() << "\n";
        }
    } else {
        std::cout << "   ? Chain failed: " << chained_result.message() << "\n";
    }
    
    std::cout << "\n";
    
    // Example 4: Performance benefits
    std::cout << "4. Performance benefits:\n";
    std::cout << "   - Success path: zero string construction overhead\n";
    std::cout << "   - Error path: message built only when accessed\n";
    std::cout << "   - Multiple error checks: message cached after first access\n";
    std::cout << "   - Memory efficient: no string allocation for success cases\n";
}

int main() {
    demonstrateResultApi();
    return 0;
}
