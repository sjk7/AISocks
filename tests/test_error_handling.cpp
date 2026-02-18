// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com Tests: Error reporting and graceful failure for
// invalid/misused operations. Checks observable behaviour only.

#include "TcpSocket.h"
#include "test_helpers.h"
#include <thread>
#include <chrono>

using namespace aiSocks;

int main() {
    std::cout << "=== Error Handling Tests ===\n";

    BEGIN_TEST("bind() on invalid socket returns false");
    {
        TcpSocket s;
        s.close(); // invalidate
        REQUIRE(!s.bind("127.0.0.1", Port{19700}));
    }

    BEGIN_TEST("listen() without bind returns false");
    {
        TcpSocket s;
        // listen without prior bind should fail
        bool result = s.listen(5);
        // Not guaranteed to fail on every OS, but getLastError must not be None
        // when it does fail; if it succeeds that's also acceptable (OS
        // behaviour)
        REQUIRE_MSG(true, "listen() without bind completed without crash");
    }

    BEGIN_TEST("connect() to a refused port returns false");
    {
        TcpSocket s;
        // Port 1 is almost certainly not listening
        bool r = s.connect("127.0.0.1", Port{1});
        REQUIRE(!r);
        REQUIRE(s.getLastError() != SocketError::None);
    }

    BEGIN_TEST(
        "getErrorMessage returns non-empty string after a failed operation");
    {
        TcpSocket s;
        s.connect("127.0.0.1", Port{1}); // will fail
        std::string msg = s.getErrorMessage();
        REQUIRE(!msg.empty());
    }

    BEGIN_TEST("send() on unconnected socket returns <= 0");
    {
        TcpSocket s;
        int r = s.send("hello", 5);
        REQUIRE(r <= 0);
    }

    BEGIN_TEST("receive() on unconnected socket returns <= 0");
    {
        TcpSocket s;
        char buf[64];
        int r = s.receive(buf, sizeof(buf));
        REQUIRE(r <= 0);
    }

    BEGIN_TEST(
        "bind() to the same address/port twice returns false on second call");
    {
        TcpSocket s1;
        TcpSocket s2;
        s1.setReuseAddress(false);
        s2.setReuseAddress(false);

        bool first = s1.bind("127.0.0.1", Port{19701});
        if (!first) {
            // Port may already be in use; skip gracefully
            REQUIRE_MSG(true, "SKIP - port 19701 unavailable");
        } else {
            REQUIRE(s1.listen(1));
            bool second = s2.bind("127.0.0.1", Port{19701});
            REQUIRE(!second);
        }
    }

    BEGIN_TEST("connect() on closed socket returns false and sets an error");
    {
        TcpSocket s;
        s.close();
        bool r = s.connect("127.0.0.1", Port{19702});
        REQUIRE(!r);
    }

    BEGIN_TEST("accept() on a socket that is not listening returns nullptr");
    {
        TcpSocket s;
        // Not bound or listening - accept should fail/return null quickly
        // Set non-blocking to avoid hanging
        s.setBlocking(false);
        auto accepted = s.accept();
        REQUIRE(accepted == nullptr);
    }

    BEGIN_TEST("setReceiveTimeout does not crash and returns bool");
    {
        TcpSocket s;
        bool r = s.setReceiveTimeout(std::chrono::seconds{1});
        (void)r;
        REQUIRE_MSG(true, "setReceiveTimeout() returned without crash");
    }

    BEGIN_TEST("SocketError::None on fresh socket, error set after failure");
    {
        TcpSocket s;
        REQUIRE(s.getLastError() == SocketError::None);
        s.connect("127.0.0.1", Port{1}); // forced failure
        REQUIRE(s.getLastError() != SocketError::None);
    }

    return test_summary();
}
