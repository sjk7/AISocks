// Tests for UnixSocket and SocketFactory Unix methods.
// Linux/macOS only — Windows builds skip this file via #ifndef _WIN32 guard.

#ifndef _WIN32
#include "UnixSocket.h"
#include "SocketFactory.h"
#include "test_helpers.h"
#include <thread>
#include <chrono>
#include <cstdio>

using namespace aiSocks;
using namespace std::chrono_literals;

static const UnixPath kPath{"/tmp/aisocks_test_unix.sock"};

int main() {
    printf("=== UnixSocket Tests ===\n");

    // Clean up any leftover socket from a previous failed run
    ::unlink(kPath.value().c_str());

    BEGIN_TEST("createUnixServer succeeds");
    {
        ::unlink(kPath.value().c_str());
        auto r = SocketFactory::createUnixServer(kPath);
        REQUIRE(r.isSuccess());
        REQUIRE(r.value().isValid());
        ::unlink(kPath.value().c_str());
    }

    BEGIN_TEST("createUnixClient connects to server");
    {
        ::unlink(kPath.value().c_str());
        auto srvResult = SocketFactory::createUnixServer(kPath);
        REQUIRE(srvResult.isSuccess());
        auto& srv = srvResult.value();

        std::thread serverThread([&srv]() {
            auto client = srv.accept();
            if (client) {
                const char msg[] = "hello";
                client->sendAll(msg, sizeof(msg) - 1);
            }
        });

        auto cliResult = SocketFactory::createUnixClient(kPath);
        REQUIRE(cliResult.isSuccess());
        auto& cli = cliResult.value();

        char buf[16] = {};
        bool ok = cli.receiveAll(buf, 5);
        REQUIRE(ok);
        REQUIRE(std::string(buf, 5) == "hello");

        serverThread.join();
        ::unlink(kPath.value().c_str());
    }

    BEGIN_TEST("createUnixPair: bidirectional communication");
    {
        auto [ra, rb] = SocketFactory::createUnixPair();
        REQUIRE(ra.isSuccess());
        REQUIRE(rb.isSuccess());

        auto& a = ra.value();
        auto& b = rb.value();

        const char ping[] = "ping";
        a.sendAll(ping, sizeof(ping) - 1);

        char buf[8] = {};
        b.receiveAll(buf, 4);
        REQUIRE(std::string(buf, 4) == "ping");

        const char pong[] = "pong";
        b.sendAll(pong, sizeof(pong) - 1);
        char buf2[8] = {};
        a.receiveAll(buf2, 4);
        REQUIRE(std::string(buf2, 4) == "pong");
    }

    return test_summary();
}
#else
int main() { return 0; }
#endif
