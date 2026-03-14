// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Tests for UnixSocket and SocketFactory Unix methods.
// Skipped entirely on platforms without AF_UNIX support.

#include "AISocksConfig.h"
#include <cstdio>

#ifdef AISOCKS_HAVE_UNIX_SOCKETS

#include "UnixSocket.h"
#include "SocketFactory.h"
#include "test_helpers.h"
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <string>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

using namespace aiSocks;

// Build a temp-directory path that works on both Windows (AF_UNIX needs Win32
// paths) and POSIX.
static std::string tempSockPath(const char* name) {
#ifdef _WIN32
    char tmp[MAX_PATH];
    DWORD len = ::GetTempPathA(MAX_PATH, tmp);
    std::string base = (len > 0) ? std::string(tmp) : "C:\\Windows\\Temp\\";
    return base + name;
#else
    return std::string("/tmp/") + name;
#endif
}

// Paths used by server-based tests.  Each test unlinks its own path.
static const std::string kSockPath = tempSockPath("aisocks_test_unix.sock");
static const std::string kSockPath2 = tempSockPath("aisocks_test_unix2.sock");

static inline void sock_unlink(const char* p) {
#ifdef _WIN32
    ::_unlink(p);
#else
    ::unlink(p);
#endif
}

#ifdef _WIN32
static bool traceUnixCloseEnabled() {
    char buf[8] = {};
    const DWORD n = ::GetEnvironmentVariableA(
        "AISOCKS_TRACE_UNIX_CLOSE", buf, static_cast<DWORD>(sizeof(buf)));
    return n > 0 && buf[0] != '0';
}
#endif

// RAII unlink: removes the path on construction and destruction.
struct ScopedUnlink {
    std::string path;
    explicit ScopedUnlink(const std::string& p) : path(p) {
        sock_unlink(p.c_str());
    }
    ~ScopedUnlink() {
#ifdef _WIN32
        if (traceUnixCloseEnabled()) {
            std::fprintf(
                stderr, "[trace] ScopedUnlink begin: %s\n", path.c_str());
            std::fflush(stderr);
        }
#endif
        sock_unlink(path.c_str());
#ifdef _WIN32
        if (traceUnixCloseEnabled()) {
            std::fprintf(
                stderr, "[trace] ScopedUnlink end: %s\n", path.c_str());
            std::fflush(stderr);
        }
#endif
    }
};

// Wait for `flag` to become true, with a 2-second deadline.
static void waitReady(const std::atomic<bool>& flag) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!flag && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
}

int main() {
    printf("=== UnixSocket Tests ===\n");

    // -----------------------------------------------------------------------
    // Happy paths
    // -----------------------------------------------------------------------

    BEGIN_TEST("createUnixServer succeeds");
    {
        ScopedUnlink guard(kSockPath);
        auto r = SocketFactory::createUnixServer(UnixPath{kSockPath});
        REQUIRE(r.isSuccess());
        REQUIRE(r.value().isValid());
    }

    BEGIN_TEST("createUnixClient connects and exchanges a short message");
    {
        ScopedUnlink guard(kSockPath);
        auto srvResult = SocketFactory::createUnixServer(UnixPath{kSockPath});
        REQUIRE(srvResult.isSuccess());

        std::atomic<bool> ready{false};
        std::thread serverThread([&]() {
            ready = true;
            auto client = srvResult.value().accept();
            if (client) {
                const char msg[] = "hello";
                client->sendAll(msg, sizeof(msg) - 1);
            }
        });

        waitReady(ready);
        auto cliResult = SocketFactory::createUnixClient(UnixPath{kSockPath});
        REQUIRE(cliResult.isSuccess());

        char buf[16] = {};
        bool ok = cliResult.value().receiveAll(buf, 5);
        REQUIRE(ok);
        REQUIRE(std::string(buf, 5) == "hello");

        serverThread.join();
    }

    BEGIN_TEST("getLocalEndpoint returns the socket path");
    {
        ScopedUnlink guard(kSockPath);
        auto srvResult = SocketFactory::createUnixServer(UnixPath{kSockPath});
        REQUIRE(srvResult.isSuccess());

        auto ep = srvResult.value().getLocalEndpoint();
        REQUIRE(ep.isSuccess());
        REQUIRE(ep.value().address == kSockPath);
        REQUIRE(ep.value().port == Port{0});
        REQUIRE(ep.value().family == AddressFamily::Unix);
    }

    BEGIN_TEST(
        "Endpoint::isLoopback() and isPrivateNetwork() return true for Unix");
    {
        ScopedUnlink guard(kSockPath);
        auto srvResult = SocketFactory::createUnixServer(UnixPath{kSockPath});
        REQUIRE(srvResult.isSuccess());

        auto ep = srvResult.value().getLocalEndpoint();
        REQUIRE(ep.isSuccess());
        REQUIRE(ep.value().isLoopback());
        REQUIRE(ep.value().isPrivateNetwork());
    }

    BEGIN_TEST(
        "Large payload (1 MB) transferred exactly via sendAll/receiveAll");
    {
        ScopedUnlink guard(kSockPath);
        auto srvResult = SocketFactory::createUnixServer(UnixPath{kSockPath});
        REQUIRE(srvResult.isSuccess());

        const std::string payload(1 * 1024 * 1024, 'Z');
        std::atomic<bool> ready{false};

        std::thread serverThread([&]() {
            ready = true;
            auto client = srvResult.value().accept();
            if (client) client->sendAll(payload.data(), payload.size());
        });

        waitReady(ready);
        auto cliResult = SocketFactory::createUnixClient(UnixPath{kSockPath});
        REQUIRE(cliResult.isSuccess());

        std::vector<char> buf(payload.size(), 0);
        bool ok = cliResult.value().receiveAll(buf.data(), payload.size());
        serverThread.join();

        REQUIRE(ok);
        REQUIRE(std::equal(buf.begin(), buf.end(), payload.begin()));
    }

    BEGIN_TEST("Echo: client sends, server echoes back, client verifies");
    {
        ScopedUnlink guard(kSockPath);
        auto srvResult = SocketFactory::createUnixServer(UnixPath{kSockPath});
        REQUIRE(srvResult.isSuccess());

        const std::string msg = "echo_me";
        std::atomic<bool> ready{false};

        std::thread serverThread([&]() {
            ready = true;
            auto client = srvResult.value().accept();
            if (client) {
                char echoBuf[64] = {};
                int r = client->receive(echoBuf, sizeof(echoBuf) - 1);
                if (r > 0) client->send(echoBuf, static_cast<size_t>(r));
            }
        });

        waitReady(ready);
        auto cliResult = SocketFactory::createUnixClient(UnixPath{kSockPath});
        REQUIRE(cliResult.isSuccess());
        auto& cli = cliResult.value();

        REQUIRE(
            cli.send(msg.data(), msg.size()) == static_cast<int>(msg.size()));

        char replyBuf[64] = {};
        int got = cli.receive(replyBuf, sizeof(replyBuf) - 1);
        serverThread.join();

        REQUIRE(got == static_cast<int>(msg.size()));
        REQUIRE(std::string(replyBuf, static_cast<size_t>(got)) == msg);
    }

    BEGIN_TEST("Multiple sequential clients accepted by one server");
    {
        ScopedUnlink guard(kSockPath);
        auto srvResult = SocketFactory::createUnixServer(UnixPath{kSockPath});
        REQUIRE(srvResult.isSuccess());

        constexpr int kClients = 3;
        std::atomic<bool> ready{false};
        std::atomic<int> accepted{0};

        std::thread serverThread([&]() {
            ready = true;
            for (int i = 0; i < kClients; ++i) {
                auto c = srvResult.value().accept();
                if (c) {
                    char token[4] = {};
                    if (c->receiveAll(token, 3)) ++accepted;
                }
            }
        });

        waitReady(ready);
        for (int i = 0; i < kClients; ++i) {
            auto cliResult
                = SocketFactory::createUnixClient(UnixPath{kSockPath});
            REQUIRE(cliResult.isSuccess());
            cliResult.value().sendAll("ok!", 3);
            // Close before next iteration so accept() unblocks for this client
            cliResult.value().close();
        }
        serverThread.join();

        REQUIRE(accepted == kClients);
    }

    BEGIN_TEST("Move semantics: original socket invalid after move");
    {
        ScopedUnlink guard(kSockPath);
        auto r = SocketFactory::createUnixServer(UnixPath{kSockPath});
        REQUIRE(r.isSuccess());

        UnixSocket moved(std::move(r.value()));
        REQUIRE(moved.isValid());
        REQUIRE(!r.value().isValid());

        UnixSocket assigned(std::move(moved));
        REQUIRE(assigned.isValid());
        REQUIRE(!moved.isValid());
    }

    BEGIN_TEST(
        "setBlocking(false): non-blocking accept returns nullptr immediately");
    {
        ScopedUnlink guard(kSockPath);
        auto srvResult = SocketFactory::createUnixServer(UnixPath{kSockPath});
        REQUIRE(srvResult.isSuccess());

        auto& srv = srvResult.value();
        REQUIRE(srv.setBlocking(false));

        auto accepted = srv.accept();
        REQUIRE(accepted == nullptr);
        REQUIRE(srv.getLastError() == SocketError::WouldBlock);
    }

    // -----------------------------------------------------------------------
    // Sad / error paths
    // -----------------------------------------------------------------------

    BEGIN_TEST("createUnixServer fails on duplicate path (no unlink between)");
    {
        ScopedUnlink guard(kSockPath);
        auto first = SocketFactory::createUnixServer(UnixPath{kSockPath});
        REQUIRE(first.isSuccess());

        // Second bind to the same path — the file still exists, must fail
        auto second = SocketFactory::createUnixServer(UnixPath{kSockPath});
        REQUIRE(second.isError());
        REQUIRE(second.error() != SocketError::None);
    }

    BEGIN_TEST("createUnixClient fails on non-existent path");
    {
        const std::string absent = tempSockPath("aisocks_no_such_socket.sock");
        sock_unlink(absent.c_str()); // ensure absent
        auto r = SocketFactory::createUnixClient(UnixPath{absent});
        REQUIRE(r.isError());
        REQUIRE(r.error() != SocketError::None);
    }

    BEGIN_TEST("createUnixServer fails when parent directory does not exist");
    {
        // Build a path into a directory that cannot exist.
        const std::string bad = tempSockPath("no_such_dir_xyz/sock.sock");
        auto r = SocketFactory::createUnixServer(UnixPath{bad});
        REQUIRE(r.isError());
        REQUIRE(r.error() != SocketError::None);
    }

    BEGIN_TEST("createUnixServer fails when path exceeds sun_path limit");
    {
        // sun_path is 104 bytes on macOS, 108 on Linux, same on Windows.
        // Build a path >= 108 chars to be rejected on all platforms.
        std::string longPath = tempSockPath("");
        longPath += std::string(110, 'x');
        longPath += ".sock";

        auto r = SocketFactory::createUnixServer(UnixPath{longPath});
        REQUIRE(r.isError());
        REQUIRE(r.error() != SocketError::None);
    }

    BEGIN_TEST("createUnixClient fails when path exceeds sun_path limit");
    {
        std::string longPath = tempSockPath("");
        longPath += std::string(110, 'x');
        longPath += ".sock";

        auto r = SocketFactory::createUnixClient(UnixPath{longPath});
        REQUIRE(r.isError());
        REQUIRE(r.error() != SocketError::None);
    }

    BEGIN_TEST(
        "receiveAll returns false on premature EOF (server closes early)");
    {
        ScopedUnlink guard(kSockPath);
        auto srvResult = SocketFactory::createUnixServer(UnixPath{kSockPath});
        REQUIRE(srvResult.isSuccess());

        constexpr size_t kSend = 16;
        constexpr size_t kWant = 64; // client asks for more than server sends
        std::atomic<bool> ready{false};

        std::thread serverThread([&]() {
            ready = true;
            auto c = srvResult.value().accept();
            if (c) {
                std::vector<char> data(kSend, 'x');
                c->sendAll(data.data(), kSend);
                c->close(); // close before client reads all bytes
            }
        });

        waitReady(ready);
        auto cliResult = SocketFactory::createUnixClient(UnixPath{kSockPath});
        REQUIRE(cliResult.isSuccess());

        std::vector<char> buf(kWant, 0);
        bool ok = cliResult.value().receiveAll(buf.data(), kWant);
        serverThread.join();

        REQUIRE(!ok); // must fail — EOF before all bytes arrived
    }

    // -----------------------------------------------------------------------
    // socketpair (POSIX only — Windows lacks socketpair())
    // -----------------------------------------------------------------------

#ifndef _WIN32
    BEGIN_TEST("createUnixPair: bidirectional communication");
    {
        auto [ra, rb] = SocketFactory::createUnixPair();
        REQUIRE(ra.isSuccess());
        REQUIRE(rb.isSuccess());

        auto& a = ra.value();
        auto& b = rb.value();

        a.sendAll("ping", 4);

        char buf[8] = {};
        b.receiveAll(buf, 4);
        REQUIRE(std::string(buf, 4) == "ping");

        b.sendAll("pong", 4);
        char buf2[8] = {};
        a.receiveAll(buf2, 4);
        REQUIRE(std::string(buf2, 4) == "pong");
    }

    BEGIN_TEST("createUnixPair: send on closed socket fails");
    {
        auto [ra, rb] = SocketFactory::createUnixPair();
        REQUIRE(ra.isSuccess());
        REQUIRE(rb.isSuccess());

        // Close one end; sending to the other should fail / return <= 0
        rb.value().close();
        REQUIRE(!rb.value().isValid());

        // Give OS a moment to propagate the close
        std::this_thread::sleep_for(std::chrono::milliseconds(5));

        // send into the now-broken pipe
        int r = ra.value().send("x", 1);
        // On a broken pipe, send returns <= 0 or the next receive does so
        // — either outcome is acceptable; the socket is no longer healthy.
        if (r > 0) {
            // Some kernels buffer the byte; a subsequent receive on the
            // closed end returns 0 (EOF).  Verify the error manifests on recv.
            char tmp[4] = {};
            int r2 = ra.value().receive(tmp, sizeof(tmp));
            REQUIRE(r2 <= 0);
        } else {
            REQUIRE(r <= 0);
        }
    }
#endif // !_WIN32

    return test_summary();
}

#else // !AISOCKS_HAVE_UNIX_SOCKETS

int main() {
    printf("=== UnixSocket Tests: SKIPPED (platform has no AF_UNIX support) "
           "===\n");
    return 0;
}

#endif // AISOCKS_HAVE_UNIX_SOCKETS
