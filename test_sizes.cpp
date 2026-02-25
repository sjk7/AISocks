#include "lib/include/Result.h"
#include "lib/include/SocketTypes.h"
#include <iostream>
#include <stdexcept>

int main() {
    std::cout << "=== Result Class Size Analysis ===" << std::endl;
    std::cout << "sizeof(ErrorInfo): " << sizeof(aiSocks::ErrorInfo) << " bytes" << std::endl;
    std::cout << "sizeof(Result<int>): " << sizeof(aiSocks::Result<int>) << " bytes" << std::endl;
    std::cout << "sizeof(Result<Endpoint>): " << sizeof(aiSocks::Result<aiSocks::Endpoint>) << " bytes" << std::endl;
    std::cout << "sizeof(Result<void>): " << sizeof(aiSocks::Result<void>) << " bytes" << std::endl;
    std::cout << "sizeof(Endpoint): " << sizeof(aiSocks::Endpoint) << " bytes" << std::endl;
    std::cout << "sizeof(std::string): " << sizeof(std::string) << " bytes" << std::endl;
    return 0;
}
