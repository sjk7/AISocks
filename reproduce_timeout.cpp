#include "SocketFactory.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <vector>

using namespace aiSocks;

int main() {
    std::cout << "Starting DNS timeout reproduction..." << std::endl;

    // We'll create many concurrent DNS requests to saturate the worker pool
    // and see if they properly time out or hang.
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([i]() {
            auto start = std::chrono::steady_clock::now();
            auto result = SocketFactory::createTcpClient(AddressFamily::IPv4,
                ConnectArgs{
                    "this.is.a.fail.test." + std::to_string(i) + ".example",
                    Port(80), Milliseconds{500}});
            auto end = std::chrono::steady_clock::now();
            auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(
                end - start)
                            .count();

            if (result.isSuccess()) {
                std::cout << "Thread " << i << " unexpectedly succeeded in "
                          << diff << "ms" << std::endl;
            } else {
                std::cout << "Thread " << i << " failed as expected in " << diff
                          << "ms. Error: " << (int)result.error()
                          << " Message: " << result.message() << std::endl;
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    std::cout << "Reproduction finished." << std::endl;
    return 0;
}
