#include <iostream>
#include <chrono>

// Manual test without includes to verify timeout behavior
// We'll link against the existing library

// Forward declarations
typedef unsigned int SocketHandle;
typedef unsigned int Port;
enum class SocketError { None = 0, Timeout = 1 };

extern "C" {
    SocketHandle socket_create();
    void socket_close(SocketHandle);
    int socket_bind(SocketHandle, const char* address, int port);
    int socket_listen(SocketHandle, int backlog);
    int socket_wait_readable(SocketHandle, int timeout_ms);
    SocketError socket_get_last_error();
    void socket_set_error(SocketError err, const char* msg);
}

int main() {
    std::cout << "Manual timeout test...\n";
    
    SocketHandle sock = socket_create();
    if (sock == 0) {
        std::cout << "Failed to create socket\n";
        return 1;
    }
    
    if (socket_bind(sock, "127.0.0.1", 18080) != 0) {
        std::cout << "Failed to bind\n";
        socket_close(sock);
        return 1;
    }
    
    if (socket_listen(sock, 1) != 0) {
        std::cout << "Failed to listen\n";
        socket_close(sock);
        return 1;
    }
    
    std::cout << "Testing waitReadable timeout (10ms)...\n";
    
    // Test timeout behavior
    int result = socket_wait_readable(sock, 10);
    
    if (result == 0) {
        std::cout << "✅ waitReadable returned 0 (timeout)\n";
        SocketError err = socket_get_last_error();
        std::cout << "SocketError code: " << static_cast<int>(err) << "\n";
        
        if (err == SocketError::Timeout) {
            std::cout << "✅ SUCCESS: Timeout error set correctly!\n";
            socket_close(sock);
            return 0;
        } else {
            std::cout << "❌ FAIL: Expected Timeout error, got " << static_cast<int>(err) << "\n";
            socket_close(sock);
            return 1;
        }
    } else {
        std::cout << "❌ FAIL: waitReadable should have timed out\n";
        socket_close(sock);
        return 1;
    }
}
