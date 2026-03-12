// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, and Java:
// https://pvs-studio.com

#include "TcpSocket.h"
#include "test_helpers.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace aiSocks;
using namespace std::chrono;
using namespace std::chrono_literals;

static void waitForActiveDnsWorkers_(size_t target, milliseconds timeout) {
    const auto deadline = steady_clock::now() + timeout;
    while (steady_clock::now() < deadline) {
        if (Socket::activeDnsWorkersForTesting() >= target) return;
        std::this_thread::sleep_for(5ms);
    }
}

int main() {
    printf("=== DNS resolution gate tests ===\n");

    BEGIN_TEST("DNS gate: queued calls consume timeout while waiting");
    {
        Socket::resetDnsTestHooksForTesting();
        Socket::setDnsTestDelayForTesting(Milliseconds{250});

        const size_t limit = Socket::dnsWorkerLimitForTesting();
        REQUIRE(limit > 0);

        std::vector<std::thread> workers;
        workers.reserve(limit);

        for (size_t i = 0; i < limit; ++i) {
            workers.emplace_back([] {
                auto s = TcpSocket::createRaw();
                (void)s.connect("this.host.does.not.exist.invalid", Port{80},
                    Milliseconds{700});
            });
        }

        waitForActiveDnsWorkers_(limit, 500ms);
        REQUIRE(Socket::activeDnsWorkersForTesting() == limit);

        auto queued = TcpSocket::createRaw();
        const auto t0 = steady_clock::now();
        bool ok = queued.connect("this.host.does.not.exist.invalid", Port{80},
            Milliseconds{60});
        const auto elapsed
            = duration_cast<milliseconds>(steady_clock::now() - t0).count();

        REQUIRE(!ok);
        REQUIRE(queued.getLastError() == SocketError::Timeout);
        REQUIRE(elapsed >= 50);

        for (auto& t : workers) t.join();

        // Give detached workers a brief chance to release slots after join.
        std::this_thread::sleep_for(20ms);
        REQUIRE(Socket::activeDnsWorkersForTesting() == 0);

        Socket::resetDnsTestHooksForTesting();
    }

    return test_summary();
}
