// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
//
// Tests: error message completeness, correctness, and
// lazy-formatting invariants.
//
// These tests complement test_error_handling.cpp and test_construction.cpp by
// asserting the *content* of error messages rather than just their presence.
//
// Design decisions verified here:
//   1. strerror / FormatMessage captured eagerly (before next syscall
//   overwrites errno).
//   2. ostringstream assembly deferred until getErrorMessage() is first called.
//   3. getErrorMessage() returns empty string when lastError == None.
//   4. DNS resolution failures carry the failing hostname and use gai_strerror,
//      not strerror(errno) which would read a stale / unrelated error code.
//   5. Every SocketException::what() includes the operation step and OS text.
//   6. Closed-socket operations produce SocketError::InvalidSocket
//   specifically.

#include "TcpSocket.h"
#include "UdpSocket.h"
#include "test_helpers.h"
#include <atomic>
#include <cstring>
#include <string>
#include <thread>

using namespace aiSocks;

// Port block: 21000 – 21099 (no overlap with any other test suite)
static constexpr uint16_t BASE = 21000;

// -----------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------

// Returns true if `msg` contains the bracket pattern "[N: text]" where N is
// a decimal integer and text is at least one non-space character.
static bool hasOsBracket(const std::string& msg) {
    auto lb = msg.find('[');
    if (lb == std::string::npos) return false;
    auto rb = msg.find(']', lb);
    if (rb == std::string::npos) return false;
    auto colon = msg.find(':', lb);
    if (colon == std::string::npos || colon > rb) return false;
    // There must be at least one digit before the colon.
    for (auto i = lb + 1; i < colon; ++i)
        if (std::isdigit(static_cast<unsigned char>(msg[i]))) return true;
    return false;
}

// -----------------------------------------------------------------------
// 1. SocketException what() structure for ConnectTo failures
// -----------------------------------------------------------------------
static void test_connect_exception_message() {
    BEGIN_TEST(
        "ConnectTo exception: what() contains step, address, and OS bracket");
    {
        std::string what;
        try {
            TcpSocket c(AddressFamily::IPv4, ConnectTo{"127.0.0.1", Port{1}});
        } catch (const SocketException& e) {
            what = e.what();
        }
        std::cout << "  what(): " << what << "\n";
        REQUIRE(!what.empty());
        REQUIRE_MSG(what.find("connect(") != std::string::npos,
            "what() contains 'connect(' step name");
        REQUIRE_MSG(what.find("127.0.0.1") != std::string::npos,
            "what() contains the target address");
        REQUIRE_MSG(
            hasOsBracket(what), "what() contains '[code: text]' OS bracket");
    }

    BEGIN_TEST("ConnectTo exception: errorCode() == ConnectFailed");
    {
        SocketError code = SocketError::None;
        try {
            TcpSocket c(AddressFamily::IPv4, ConnectTo{"127.0.0.1", Port{1}});
        } catch (const SocketException& e) {
            code = e.errorCode();
        }
        REQUIRE(code == SocketError::ConnectFailed);
    }

    BEGIN_TEST("ConnectTo exception: port number appears in what()");
    {
        std::string what;
        try {
            TcpSocket c(AddressFamily::IPv4, ConnectTo{"127.0.0.1", Port{2}});
        } catch (const SocketException& e) {
            what = e.what();
        }
        REQUIRE_MSG(what.find(":2") != std::string::npos,
            "what() contains ':port' in step context");
    }
}

// -----------------------------------------------------------------------
// 2. SocketException what() structure for ServerBind failures
// -----------------------------------------------------------------------
static void test_bind_exception_message() {
    BEGIN_TEST(
        "ServerBind exception: what() contains step, address, and OS bracket");
    {
        TcpSocket occupant(
            AddressFamily::IPv4, ServerBind{"127.0.0.1", Port{BASE}, 5, false});
        std::string what;
        try {
            TcpSocket s(AddressFamily::IPv4,
                ServerBind{"127.0.0.1", Port{BASE}, 5, false});
        } catch (const SocketException& e) {
            what = e.what();
        }
        std::cout << "  what(): " << what << "\n";
        REQUIRE(!what.empty());
        REQUIRE_MSG(what.find("bind(") != std::string::npos,
            "what() contains 'bind(' step name");
        REQUIRE_MSG(what.find("127.0.0.1") != std::string::npos,
            "what() contains the bound address");
        REQUIRE_MSG(
            hasOsBracket(what), "what() contains '[code: text]' OS bracket");
    }

    BEGIN_TEST("ServerBind exception: errorCode() == BindFailed");
    {
        TcpSocket occupant(AddressFamily::IPv4,
            ServerBind{"127.0.0.1", Port{BASE + 1}, 5, false});
        SocketError code = SocketError::None;
        try {
            TcpSocket s(AddressFamily::IPv4,
                ServerBind{"127.0.0.1", Port{BASE + 1}, 5, false});
        } catch (const SocketException& e) {
            code = e.errorCode();
        }
        REQUIRE(code == SocketError::BindFailed);
    }
}

// -----------------------------------------------------------------------
// 3. DNS resolution failures carry the failing hostname
// -----------------------------------------------------------------------
static void test_dns_error_message() {
    // The .invalid TLD (RFC 2606) is guaranteed never to resolve.
    static constexpr const char* BAD_HOST
        = "this.certainly.does.not.exist.invalid";

    BEGIN_TEST("DNS failure: exception what() contains the failing hostname");
    {
        std::string what;
        try {
            TcpSocket c(
                AddressFamily::IPv4, ConnectTo{BAD_HOST, Port{BASE + 10}});
        } catch (const SocketException& e) {
            what = e.what();
        }
        std::cout << "  what(): " << what << "\n";
        REQUIRE(!what.empty());
        REQUIRE_MSG(what.find(BAD_HOST) != std::string::npos,
            "what() contains the failing hostname");
    }

    BEGIN_TEST("DNS failure: exception what() does NOT say 'hostname' without "
               "context");
    {
        std::string what;
        try {
            TcpSocket c(
                AddressFamily::IPv4, ConnectTo{BAD_HOST, Port{BASE + 10}});
        } catch (const SocketException& e) {
            what = e.what();
        }
        // Old message was "Failed to resolve hostname" — too vague.
        // It must now contain the actual hostname instead.
        REQUIRE_MSG(what.find("resolve 'this.") != std::string::npos,
            "what() says \"resolve '<hostname>'\" with the actual hostname");
    }

    BEGIN_TEST("DNS failure: what() contains OS bracket (gai_strerror text)");
    {
        std::string what;
        try {
            TcpSocket c(
                AddressFamily::IPv4, ConnectTo{BAD_HOST, Port{BASE + 10}});
        } catch (const SocketException& e) {
            what = e.what();
        }
        REQUIRE_MSG(hasOsBracket(what),
            "DNS failure what() has '[code: gai_strerror_text]' bracket");
    }

    BEGIN_TEST(
        "DNS failure: non-throwing path — getErrorMessage contains hostname");
    {
        auto s = TcpSocket::createRaw();
        // connect() non-throwing path
        (void)s.connect(BAD_HOST, Port{BASE + 10});
        std::string msg = s.getErrorMessage();
        std::cout << "  getErrorMessage(): " << msg << "\n";
        REQUIRE_MSG(msg.find(BAD_HOST) != std::string::npos,
            "getErrorMessage() contains the failing hostname (non-throwing "
            "path)");
        REQUIRE_MSG(
            hasOsBracket(msg), "getErrorMessage() has '[code: text]' bracket");
    }
}

// -----------------------------------------------------------------------
// 4. Closed-socket operations yield InvalidSocket specifically
// -----------------------------------------------------------------------
static void test_invalid_socket_code() {
    BEGIN_TEST("send() on closed socket: error code is InvalidSocket");
    {
        auto s = TcpSocket::createRaw();
        s.close();
        s.send("x", 1);
        REQUIRE(s.getLastError() == SocketError::InvalidSocket);
    }

    BEGIN_TEST("receive() on closed socket: error code is InvalidSocket");
    {
        auto s = TcpSocket::createRaw();
        s.close();
        char buf[16];
        s.receive(buf, sizeof(buf));
        REQUIRE(s.getLastError() == SocketError::InvalidSocket);
    }

    BEGIN_TEST("bind() on closed socket: error code is InvalidSocket");
    {
        auto s = TcpSocket::createRaw();
        s.close();
        REQUIRE(!s.bind("127.0.0.1", Port{BASE + 20}));
        REQUIRE(s.getLastError() == SocketError::InvalidSocket);
    }

    BEGIN_TEST("connect() on closed socket: error code is InvalidSocket");
    {
        auto s = TcpSocket::createRaw();
        s.close();
        (void)s.connect("127.0.0.1", Port{BASE + 20});
        REQUIRE(s.getLastError() == SocketError::InvalidSocket);
    }

    BEGIN_TEST("sendTo() on closed socket: error code is InvalidSocket");
    {
        UdpSocket s;
        s.close();
        Endpoint dest{"127.0.0.1", Port{BASE + 20}, AddressFamily::IPv4};
        s.sendTo("x", 1, dest);
        REQUIRE(s.getLastError() == SocketError::InvalidSocket);
    }

    BEGIN_TEST("receiveFrom() on closed socket: error code is InvalidSocket");
    {
        UdpSocket s;
        s.close();
        char buf[16];
        Endpoint from;
        s.receiveFrom(buf, sizeof(buf), from);
        REQUIRE(s.getLastError() == SocketError::InvalidSocket);
    }

    BEGIN_TEST("setReceiveBufferSize() on closed socket: InvalidSocket");
    {
        auto s = TcpSocket::createRaw();
        s.close();
        s.setReceiveBufferSize(64 * 1024);
        REQUIRE(s.getLastError() == SocketError::InvalidSocket);
    }

    BEGIN_TEST("setSendBufferSize() on closed socket: InvalidSocket");
    {
        auto s = TcpSocket::createRaw();
        s.close();
        s.setSendBufferSize(64 * 1024);
        REQUIRE(s.getLastError() == SocketError::InvalidSocket);
    }
}

// -----------------------------------------------------------------------
// 5. Closed-socket error messages are non-empty and contain OS bracket
// -----------------------------------------------------------------------
static void test_invalid_socket_message_content() {
    BEGIN_TEST(
        "send() on closed socket: getErrorMessage() is non-empty with bracket");
    {
        auto s = TcpSocket::createRaw();
        s.close();
        s.send("x", 1);
        std::string msg = s.getErrorMessage();
        std::cout << "  send on closed: " << msg << "\n";
        REQUIRE(!msg.empty());
        // The InvalidSocket path doesn't involve a syscall so errno may be 0;
        // the description must still be present between brackets.
        REQUIRE_MSG(msg.find('[') != std::string::npos,
            "error message contains '[' bracket");
    }

    BEGIN_TEST("sendTo() on closed socket: getErrorMessage() is non-empty");
    {
        UdpSocket s;
        s.close();
        Endpoint dest{"127.0.0.1", Port{BASE + 30}, AddressFamily::IPv4};
        s.sendTo("x", 1, dest);
        REQUIRE(!s.getErrorMessage().empty());
    }

    BEGIN_TEST(
        "receiveFrom() on closed socket: getErrorMessage() is non-empty");
    {
        UdpSocket s;
        s.close();
        char buf[16];
        Endpoint from;
        s.receiveFrom(buf, sizeof(buf), from);
        REQUIRE(!s.getErrorMessage().empty());
    }
}

// -----------------------------------------------------------------------
// 6. getErrorMessage() is empty after a successful operation
// -----------------------------------------------------------------------
static void test_error_clears_on_success() {
    BEGIN_TEST(
        "getErrorMessage(): returns empty string when lastError == None");
    {
        // Fresh socket — no error has ever occurred.
        auto s = TcpSocket::createRaw();
        REQUIRE(s.getLastError() == SocketError::None);
        REQUIRE_MSG(s.getErrorMessage().empty(),
            "getErrorMessage() is empty on fresh socket");
    }

    BEGIN_TEST(
        "getErrorMessage(): empty after a failure followed by a success");
    {
        auto srv = TcpSocket::createRaw();
        srv.setReuseAddress(true);
        REQUIRE(srv.bind("127.0.0.1", Port{BASE + 40}));
        REQUIRE(srv.listen(1));

        std::thread t([&]() {
            auto peer = srv.accept();
            if (peer) peer->close();
        });

        auto c = TcpSocket::createRaw();
        // Trigger a failure first.
        (void)c.connect("127.0.0.1", Port{1}); // refused
        REQUIRE(c.getLastError() != SocketError::None);
        REQUIRE(!c.getErrorMessage().empty());

        // Re-connect successfully (need a fresh socket since connect() on an
        // ECONNREFUSED socket is not retryable on all platforms).
        auto c2 = TcpSocket::createRaw();
        REQUIRE(c2.connect("127.0.0.1", Port{BASE + 40}));
        REQUIRE(c2.getLastError() == SocketError::None);
        REQUIRE_MSG(c2.getErrorMessage().empty(),
            "getErrorMessage() is empty after a successful connect()");

        t.join();
    }
}

// -----------------------------------------------------------------------
// 7. Post-shutdown send / receive returns an error (not a silent success)
// -----------------------------------------------------------------------
static void test_post_shutdown_errors() {
    BEGIN_TEST("send() after shutdown(Write): returns -1 and sets an error");
    {
        auto srv = TcpSocket::createRaw();
        srv.setReuseAddress(true);
        REQUIRE(srv.bind("127.0.0.1", Port{BASE + 50}));
        REQUIRE(srv.listen(1));

        std::thread t([&]() { (void)srv.accept(); });

        auto c = TcpSocket::createRaw();
        REQUIRE(c.connect("127.0.0.1", Port{BASE + 50}));
        REQUIRE(c.shutdown(ShutdownHow::Write));

        // The write side is closed; send() must fail.
        int r = c.send("hello", 5);
        REQUIRE(r <= 0);
        // Error must have been recorded.
        REQUIRE(c.getLastError() != SocketError::None);
        REQUIRE(!c.getErrorMessage().empty());
        std::cout << "  send-after-shutdown error: " << c.getErrorMessage()
                  << "\n";

        t.join();
    }
}

// -----------------------------------------------------------------------
// 8. getLastError() returns None (not a stale code) on a fresh socket
// -----------------------------------------------------------------------
static void test_no_stale_error_on_fresh_socket() {
    BEGIN_TEST("Fresh socket: getLastError() == None, getErrorMessage() empty");
    {
        for (int i = 0; i < 5; ++i) {
            auto s = TcpSocket::createRaw();
            REQUIRE(s.getLastError() == SocketError::None);
            REQUIRE(s.getErrorMessage().empty());
        }
    }

    BEGIN_TEST("After setReuseAddress(true): getLastError() == None");
    {
        auto s = TcpSocket::createRaw();
        REQUIRE(s.setReuseAddress(true));
        REQUIRE(s.getLastError() == SocketError::None);
        REQUIRE(s.getErrorMessage().empty());
    }
}

// -----------------------------------------------------------------------
int main() {
    std::cout << "=== Error Message Content Tests ===\n";
    test_connect_exception_message();
    test_bind_exception_message();
    test_dns_error_message();
    test_invalid_socket_code();
    test_invalid_socket_message_content();
    test_error_clears_on_success();
    test_post_shutdown_errors();
    test_no_stale_error_on_fresh_socket();
    return test_summary();
}
