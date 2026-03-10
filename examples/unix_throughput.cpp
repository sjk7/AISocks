// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Measures raw AF_UNIX stream throughput: one sender pushes data into one
// receiver as fast as the kernel allows, then reports MB/s or GB/s.
//
// Strategy:
//   - POSIX (Linux / macOS): use socketpair() — no filesystem path needed,
//     lowest possible overhead.
//   - Windows: bind a named socket in the system temp directory, connect to
//     it, then send from one thread and receive on the other.
//
// Both ends run on separate threads so send and receive never block each other.
// The sender fills a 256 kB buffer in a tight loop; the receiver drains into
// a 256 kB buffer and accumulates byte count.  After `durationSecs` the
// sender stops and the receiver tallies the final count.
//
// Usage:  unix_throughput [seconds]   (default: 5)

#include "AISocksConfig.h"

#ifndef AISOCKS_HAVE_UNIX_SOCKETS
#include <cstdio>
int main() {
    printf("AF_UNIX sockets are not available on this platform/SDK.\n");
    return 1;
}
#else

#include "SocketFactory.h"
#include "SocketTypes.h"
#include "UnixSocket.h"

#include <atomic>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <thread>

#ifdef _WIN32
#include <windows.h> // GetTempPathA
#else
#include <unistd.h>  // unlink
#endif

using namespace aiSocks;
using Clock = std::chrono::steady_clock;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void printRate(double seconds, uint64_t bytes) {
    double bps = static_cast<double>(bytes) / seconds;
    if (bps >= 1e9)
        printf("%.2f GB/s  (%.2f Gbit/s)", bps / 1e9, bps * 8.0 / 1e9);
    else if (bps >= 1e6)
        printf("%.2f MB/s  (%.2f Mbit/s)", bps / 1e6, bps * 8.0 / 1e6);
    else if (bps >= 1e3)
        printf("%.2f kB/s", bps / 1e3);
    else
        printf("%.0f B/s", bps);
}

static constexpr size_t kBufSize = 256 * 1024; // 256 KB I/O buffer

// ---------------------------------------------------------------------------
// Sender thread: push data until stopFlag is set, then set doneSending.
// ---------------------------------------------------------------------------
static void senderThread(UnixSocket* sock,
                         std::atomic<bool>* stopFlag,
                         std::atomic<bool>* doneSending,
                         std::atomic<uint64_t>* sentBytes) {
    static const char fill = 0x55;
    char buf[kBufSize];
    memset(buf, fill, sizeof(buf));

    uint64_t total = 0;
    while (!stopFlag->load(std::memory_order_relaxed)) {
        int n = sock->send(buf, sizeof(buf));
        if (n > 0) {
            total += static_cast<uint64_t>(n);
        } else {
            auto err = sock->getLastError();
            if (err == SocketError::WouldBlock) {
                // Kernel send buffer full — yield and retry.
                std::this_thread::yield();
            } else {
                break; // real error or peer closed
            }
        }
    }
    sentBytes->store(total, std::memory_order_relaxed);
    doneSending->store(true, std::memory_order_release);
}

// ---------------------------------------------------------------------------
// Run the benchmark given two connected UnixSockets.
// ---------------------------------------------------------------------------
static void runBenchmark(UnixSocket sender, UnixSocket receiver,
                         int durationSecs) {
    // Set both ends non-blocking (default, but be explicit).
    (void)sender.setBlocking(false);
    (void)receiver.setBlocking(false);

    // Maximise kernel buffer sizes.
    (void)sender.setSendBufferSize(4 * 1024 * 1024);
    (void)receiver.setReceiveBufferSize(4 * 1024 * 1024);

    std::atomic<bool>  stopFlag{false};
    std::atomic<bool>  doneSending{false};
    std::atomic<uint64_t> sentBytes{0};

    // Kick off the sender thread.
    UnixSocket* pSender = &sender;
    std::thread tx(senderThread, pSender, &stopFlag, &doneSending, &sentBytes);

    // Receiver loop on this thread.
    char buf[kBufSize];
    uint64_t recvTotal = 0;

    auto deadline = Clock::now() + std::chrono::seconds(durationSecs);

    while (Clock::now() < deadline) {
        int n = receiver.receive(buf, sizeof(buf));
        if (n > 0) {
            recvTotal += static_cast<uint64_t>(n);
        } else {
            auto err = receiver.getLastError();
            if (err == SocketError::WouldBlock) {
                std::this_thread::yield();
            } else {
                break;
            }
        }
    }

    // Signal sender to stop, then drain whatever is still in flight.
    stopFlag.store(true, std::memory_order_relaxed);
    tx.join();

    // Drain residual data the sender wrote before it noticed the stop flag.
    while (true) {
        int n = receiver.receive(buf, sizeof(buf));
        if (n > 0) {
            recvTotal += static_cast<uint64_t>(n);
        } else {
            break; // WouldBlock or closed
        }
    }

    printf("Sent    : %" PRIu64 " bytes\n", sentBytes.load());
    printf("Received: %" PRIu64 " bytes\n", recvTotal);
    printf("Duration: %d s\n", durationSecs);
    printf("Recv throughput: ");
    printRate(static_cast<double>(durationSecs), recvTotal);
    printf("\n");
    printf("Send throughput: ");
    printRate(static_cast<double>(durationSecs), sentBytes.load());
    printf("\n");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    int durationSecs = (argc > 1) ? atoi(argv[1]) : 5;
    if (durationSecs <= 0) durationSecs = 5;

    printf("=== AF_UNIX Raw Throughput Benchmark ===\n");
    printf("Duration: %d seconds\n\n", durationSecs);

#ifndef _WIN32
    // -----------------------------------------------------------------------
    // POSIX path: use socketpair() — no filesystem, minimal overhead.
    // -----------------------------------------------------------------------
    printf("Method: socketpair(AF_UNIX)\n\n");

    auto [r0, r1] = SocketFactory::createUnixPair();
    if (!r0 || !r1) {
        printf("[error] socketpair failed: %s\n",
               (!r0 ? r0.message().c_str() : r1.message().c_str()));
        return 1;
    }

    runBenchmark(std::move(r0.value()), std::move(r1.value()), durationSecs);

#else
    // -----------------------------------------------------------------------
    // Windows path: bind + connect in the system temp directory.
    // -----------------------------------------------------------------------
    char tmpDir[MAX_PATH];
    GetTempPathA(MAX_PATH, tmpDir);
    std::string sockPath = std::string(tmpDir) + "aisocks_throughput.sock";

    // Clean up any leftover socket file from a previous run.
    (void)::_unlink(sockPath.c_str());

    printf("Method: AF_UNIX bind/connect (%s)\n\n", sockPath.c_str());

    auto serverResult = SocketFactory::createUnixServer(UnixPath{sockPath});
    if (!serverResult) {
        printf("[error] createUnixServer failed: %s\n",
               serverResult.message().c_str());
        return 1;
    }
    UnixSocket serverSock(std::move(serverResult.value()));

    auto clientResult = SocketFactory::createUnixClient(UnixPath{sockPath});
    if (!clientResult) {
        printf("[error] createUnixClient failed: %s\n",
               clientResult.message().c_str());
        return 1;
    }
    UnixSocket clientSock(std::move(clientResult.value()));

    auto acceptedOpt = serverSock.accept();
    if (!acceptedOpt) {
        printf("[error] accept failed\n");
        return 1;
    }
    UnixSocket acceptedSock(std::move(*acceptedOpt));

    // clientSock sends, acceptedSock receives.
    runBenchmark(std::move(clientSock), std::move(acceptedSock), durationSecs);

    ::_unlink(sockPath.c_str());
#endif

    return 0;
}

#endif // AISOCKS_HAVE_UNIX_SOCKETS
