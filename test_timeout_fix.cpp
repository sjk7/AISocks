#include "lib/include/Socket.h"
#include "lib/include/TcpSocket.h"
#include <iostream>
#include <chrono>

using namespace aiSocks;

int main() {
    std::cout << "Testing timeout error handling fix...\n";
    
    try {
        auto socket = TcpSocket::createRaw();
        socket.setReuseAddress(true);
        socket.bind("127.0.0.1", Port{18080});
        socket.listen(1);
        
        std::cout << "Socket listening, testing waitReadable timeout...\n";
        
        // This should timeout and set SocketError::Timeout
        bool result = socket.waitReadable(std::chrono::milliseconds(10));
        
        if (!result) {
            std::cout << "waitReadable returned false (expected)\n";
            std::cout << "Last error: " << static_cast<int>(socket.getLastError()) << "\n";
            std::cout << "Expected timeout error: " << static_cast<int>(SocketError::Timeout) << "\n";
            
            if (socket.getLastError() == SocketError::Timeout) {
                std::cout << "✅ SUCCESS: Timeout error set correctly!\n";
                return 0;
            } else {
                std::cout << "❌ FAIL: Wrong error type - got " << static_cast<int>(socket.getLastError()) << "\n";
                return 1;
            }
        } else {
            std::cout << "❌ FAIL: waitReadable should have timed out\n";
            return 1;
        }
    } catch (const std::exception& e) {
        std::cout << "Exception: " << e.what() << "\n";
        return 1;
    }
}
