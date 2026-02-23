// Example demonstrating lazy error message construction in Result<T>

#include "Result.h"
#include "Socket.h"
#include <iostream>

using namespace aiSocks;

int main() {
    std::cout << "=== Testing Result<T> Lazy Error Message Construction ===\n\n";
    
    // Test 1: Success case (no error message construction)
    std::cout << "1. Success case:\n";
    auto success = Result<int>::success(42);
    std::cout << "   isSuccess: " << success.isSuccess() << "\n";
    std::cout << "   value: " << success.value() << "\n";
    std::cout << "   message: '" << success.message() << "' (empty for success)\n\n";
    
    // Test 2: Error case - message not constructed yet
    std::cout << "2. Error case - before accessing message:\n";
    auto error = Result<int>::failure(
        SocketError::ConnectFailed, 
        "connect()", 
        10061,  // WSAECONNREFUSED on Windows
        false
    );
    std::cout << "   isSuccess: " << error.isSuccess() << "\n";
    std::cout << "   error: " << static_cast<int>(error.error()) << "\n";
    std::cout << "   message constructed: " << (error.message().empty() ? "no" : "yes") << "\n\n";
    
    // Test 3: Error case - first access triggers message construction
    std::cout << "3. Error case - first message access (triggers construction):\n";
    const std::string& msg1 = error.message();
    std::cout << "   message: '" << msg1 << "'\n";
    std::cout << "   message constructed: " << (msg1.empty() ? "no" : "yes") << "\n\n";
    
    // Test 4: Error case - subsequent access uses cached message
    std::cout << "4. Error case - second message access (uses cache):\n";
    const std::string& msg2 = error.message();
    std::cout << "   message: '" << msg2 << "'\n";
    std::cout << "   same address as first: " << (&msg1 == &msg2 ? "yes" : "no") << "\n\n";
    
    // Test 5: Result<void> specialization
    std::cout << "5. Result<void> specialization:\n";
    auto void_success = Result<void>::success();
    auto void_error = Result<void>::failure(
        SocketError::BindFailed, 
        "bind()", 
        98,  // EADDRINUSE on Linux
        false
    );
    
    std::cout << "   void success: " << void_success.isSuccess() << "\n";
    std::cout << "   void error: " << void_error.isError() << "\n";
    std::cout << "   void error message: '" << void_error.message() << "'\n\n";
    
    // Test 6: Performance comparison (conceptual)
    std::cout << "6. Performance benefits:\n";
    std::cout << "   - Success cases: zero string construction overhead\n";
    std::cout << "   - Error cases: message built only when needed\n";
    std::cout << "   - Multiple accesses: reuse cached message\n";
    std::cout << "   - Memory: only allocate when error occurs\n\n";
    
    // Test 7: Demonstrate with different error types
    std::cout << "7. Different error types:\n";
    auto timeout_error = Result<std::string>::failure(
        SocketError::Timeout, 
        "connect()", 
        0,  // No system code for timeout
        false
    );
    auto invalid_error = Result<std::string>::failure(
        SocketError::InvalidSocket, 
        "getsockopt()", 
        10038,  // WSAENOTSOCK on Windows
        false
    );
    
    std::cout << "   timeout: '" << timeout_error.message() << "'\n";
    std::cout << "   invalid: '" << invalid_error.message() << "'\n";
    
    return 0;
}
