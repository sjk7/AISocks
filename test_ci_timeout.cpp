#include "lib/include/Socket.h"
#include "lib/include/TcpSocket.h"
#include <iostream>
#include <chrono>

using namespace aiSocks;

int main() {
    std::cout << "Testing CI timeout fix...\n";
    
    try {
        auto socket = TcpSocket::createRaw();
        socket.setReuseAddress(true);
        socket.bind("127.0.0.1", Port{18080});
        socket.listen(1);
        
        std::cout << "Socket listening, testing waitReadable timeout (10ms)...\n";
        
        // This should timeout and set SocketError::Timeout
        bool result = socket.waitReadable(std::chrono::milliseconds(10));
        
        if (!result) {
            std::cout << "✅ waitReadable returned false (timeout expected)\n";
            SocketError err = socket.getLastError();
            std::cout << "SocketError code: " << static_cast<int>(err) << "\n";
            
            if (err == SocketError::Timeout) {
                std::cout << "✅ SUCCESS: SocketError::Timeout set correctly!\n";
                return 0;
            } else {
                std::cout << "❌ FAIL: Expected SocketError::Timeout, got " << static_cast<int>(err) << "\n";
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
