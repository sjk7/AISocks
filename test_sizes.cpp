#include "lib/include/Result.h"
#include "lib/include/SocketTypes.h"
#include <cstdio>
#include <stdexcept>

int main() {
    printf("=== Result Class Size Analysis ===\n");
    printf("sizeof(ErrorInfo): %zu bytes\n", sizeof(aiSocks::ErrorInfo));
    printf("sizeof(Result<int>): %zu bytes\n", sizeof(aiSocks::Result<int>));
    printf("sizeof(Result<Endpoint>): %zu bytes\n", sizeof(aiSocks::Result<aiSocks::Endpoint>));
    printf("sizeof(Result<void>): %zu bytes\n", sizeof(aiSocks::Result<void>));
    printf("sizeof(Endpoint): %zu bytes\n", sizeof(aiSocks::Endpoint));
    printf("sizeof(std::string): %zu bytes\n", sizeof(std::string));
    return 0;
}
