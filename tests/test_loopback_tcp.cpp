// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
//
// Tests: End-to-end TCP send/receive over the loopback
// interface. Checks observable behaviour only.

#include "Socket.h"
#include "test_helpers.h"
#include <thread>
#include <chrono>
#include <cstring>
#include <vector>
#include <string>
#include <atomic>

using namespace aiSocks;

static const uint16_t BASE_PORT = 19400;

// Helper: spin up a server that accepts one connection, sends `payload`, then
// closes.
static void server_send(
    Port port, const std::string& payload, std::atomic<bool>& ready) {
    Socket srv(SocketType::TCP, AddressFamily::IPv4);
    srv.setReuseAddress(true);
    if (!srv.bind("127.0.0.1", port) || !srv.listen(1)) return;
    ready = true;
    auto client = srv.accept();
    if (client) {
        size_t sent = 0;
        while (sent < payload.size()) {
            int r = client->send(payload.data() + sent, payload.size() - sent);
            if (r <= 0) break;
            sent += r; //-V101
        }
        client->close();
    }
}

// Helper: recv all bytes until connection closes, store in out.
static void recv_all(Socket& s, std::string& out) {
    char buf[4096];
    while (true) {
        int r = s.receive(buf, sizeof(buf));
        if (r <= 0) break;
        out.append(buf, r); //-V106
    }
}

int main() {
    std::cout << "=== TCP Loopback Communication Tests ===\n";

    BEGIN_TEST("Client can connect to a listening server");
    {
        Socket srv(SocketType::TCP, AddressFamily::IPv4);
        srv.setReuseAddress(true);
        REQUIRE(srv.bind("127.0.0.1", Port{BASE_PORT}));
        REQUIRE(srv.listen(1));

        std::thread t([&]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            Socket c(SocketType::TCP, AddressFamily::IPv4);
            c.connect("127.0.0.1", Port{BASE_PORT});
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        });

        auto accepted = srv.accept();
        t.join();
        REQUIRE(accepted != nullptr);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    BEGIN_TEST("Server can send data, client receives it exactly");
    {
        const std::string message = "Hello, aiSocks!";
        std::atomic<bool> ready{false};

        std::thread srvThread(
            [&]() { server_send(Port{BASE_PORT + 1}, message, ready); });

        // Wait for server to be ready
        auto deadline
            = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!ready && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));

        Socket client(SocketType::TCP, AddressFamily::IPv4);
        REQUIRE(client.connect("127.0.0.1", Port{BASE_PORT + 1}));

        std::string received;
        recv_all(client, received);
        srvThread.join();

        REQUIRE(received == message);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    BEGIN_TEST("Large payload is transferred completely");
    {
        const std::string payload(1 * 1024 * 1024, 'Z'); // 1 MB
        std::atomic<bool> ready{false};

        std::thread srvThread(
            [&]() { server_send(Port{BASE_PORT + 2}, payload, ready); });

        auto deadline
            = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!ready && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));

        Socket client(SocketType::TCP, AddressFamily::IPv4);
        REQUIRE(client.connect("127.0.0.1", Port{BASE_PORT + 2}));

        std::string received;
        recv_all(client, received);
        srvThread.join();

        REQUIRE(received.size() == payload.size());
        REQUIRE(received == payload);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    BEGIN_TEST("Client can send to server and server echoes back");
    {
        const std::string msg = "ping";
        std::atomic<bool> ready{false};

        std::thread srvThread([&]() {
            Socket srv(SocketType::TCP, AddressFamily::IPv4);
            srv.setReuseAddress(true);
            REQUIRE(srv.bind("127.0.0.1", Port{BASE_PORT + 3}));
            REQUIRE(srv.listen(1));
            ready = true;
            auto c = srv.accept();
            if (c) {
                char buf[256] = {};
                int r = c->receive(buf, sizeof(buf) - 1);
                if (r > 0) c->send(buf, r); // echo
                c->close();
            }
        });

        auto deadline
            = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!ready && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));

        Socket client(SocketType::TCP, AddressFamily::IPv4);
        REQUIRE(client.connect("127.0.0.1", Port{BASE_PORT + 3}));
        REQUIRE(client.send(msg.data(), msg.size())
            == static_cast<int>(msg.size()));

        char buf[256] = {};
        int r = client.receive(buf, sizeof(buf) - 1);
        srvThread.join();

        REQUIRE(r == static_cast<int>(msg.size()));
        REQUIRE(std::string(buf, r) == msg);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    BEGIN_TEST("setReuseAddress allows rapid re-bind on same port");
    {
        // First server: bind, listen, close
        {
            Socket srv(SocketType::TCP, AddressFamily::IPv4);
            srv.setReuseAddress(true);
            REQUIRE(srv.bind("127.0.0.1", Port{BASE_PORT + 4}));
            REQUIRE(srv.listen(1));
            srv.close();
        }
        // Short wait for OS to release
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        // Second server: should be able to re-bind
        Socket srv2(SocketType::TCP, AddressFamily::IPv4);
        srv2.setReuseAddress(true);
        REQUIRE(srv2.bind("127.0.0.1", Port{BASE_PORT + 4}));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    BEGIN_TEST("IPv6 loopback send/receive works");
    {
        const std::string message = "Hello IPv6!";
        std::atomic<bool> ready{false};

        std::thread srvThread([&]() {
            Socket srv(SocketType::TCP, AddressFamily::IPv6);
            srv.setReuseAddress(true);
            if (!srv.bind("::1", Port{BASE_PORT + 5}) || !srv.listen(1)) {
                ready = true; // Signal even on failure so client doesn't block
                return;
            }
            ready = true;
            auto client = srv.accept();
            if (client) {
                size_t sent = 0;
                while (sent < message.size()) {
                    int r = client->send(
                        message.data() + sent, message.size() - sent);
                    if (r <= 0) break;
                    sent += r; //-V101
                }
                client->close();
            }
        });

        auto deadline
            = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!ready && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));

        Socket client(SocketType::TCP, AddressFamily::IPv6);
        bool connected = client.connect("::1", Port{BASE_PORT + 5});
        srvThread.join();

        if (!connected) {
            REQUIRE_MSG(true, "SKIP - IPv6 not available on this system");
        } else {
            std::string received;
            recv_all(client, received);
            REQUIRE(received == message);
        }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    BEGIN_TEST("receiveAll reads exactly N bytes even across partial recvs");
    {
        // Server sends a 256-byte payload in 16-byte chunks to exercise the
        // receiveAll loop.  Client calls receiveAll(256) and checks the result.
        constexpr size_t PAYLOAD = 256;
        std::vector<char> expected(PAYLOAD);
        for (size_t i = 0; i < PAYLOAD; ++i) expected[i] = static_cast<char>(i);

        std::atomic<bool> ready{false};
        std::thread srvThread([&]() {
            Socket srv(SocketType::TCP, AddressFamily::IPv4);
            srv.setReuseAddress(true);
            if (!srv.bind("127.0.0.1", Port{BASE_PORT + 6}) || !srv.listen(1)) {
                ready = true;
                return;
            }
            ready = true;
            auto cli = srv.accept();
            if (cli) {
                constexpr size_t CHUNK = 16;
                for (size_t off = 0; off < PAYLOAD; off += CHUNK)
                    cli->sendAll(expected.data() + off, CHUNK);
                cli->close();
            }
        });

        auto deadline
            = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!ready && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));

        Socket client(SocketType::TCP, AddressFamily::IPv4);
        REQUIRE(client.connect("127.0.0.1", Port{BASE_PORT + 6}));

        std::vector<char> buf(PAYLOAD, 0);
        bool ok = client.receiveAll(buf.data(), PAYLOAD);
        srvThread.join();

        REQUIRE(ok);
        REQUIRE(buf == expected);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    BEGIN_TEST("receiveAll returns false on premature EOF");
    {
        // Server closes after sending only half the expected bytes.
        constexpr size_t SEND = 32;
        constexpr size_t WANT = 64; // client asks for more than available

        std::atomic<bool> ready{false};
        std::thread srvThread([&]() {
            Socket srv(SocketType::TCP, AddressFamily::IPv4);
            srv.setReuseAddress(true);
            if (!srv.bind("127.0.0.1", Port{BASE_PORT + 7}) || !srv.listen(1)) {
                ready = true;
                return;
            }
            ready = true;
            auto cli = srv.accept();
            if (cli) {
                std::vector<char> data(SEND, 'x');
                cli->sendAll(data.data(), SEND);
                cli->close(); // close before client has read everything
            }
        });

        auto deadline
            = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!ready && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));

        Socket client(SocketType::TCP, AddressFamily::IPv4);
        REQUIRE(client.connect("127.0.0.1", Port{BASE_PORT + 7}));

        std::vector<char> buf(WANT, 0);
        bool ok = client.receiveAll(buf.data(), WANT);
        srvThread.join();

        REQUIRE(!ok);
        REQUIRE(client.getLastError() == SocketError::ConnectionReset);
    }

    return test_summary();
}
