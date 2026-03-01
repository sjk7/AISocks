// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Measures raw receive throughput from an HTTP/1.1 server.
// One connection, HTTP pipelining: keeps the send pipe full and drains
// the receive side as fast as possible — measures bytes/sec, not req/sec.
//
// Usage: throughput_client [host] [port] [seconds]
//   Defaults: 127.0.0.1  8080  10

#include "SocketFactory.h"
#include "TcpSocket.h"
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace aiSocks;
using Clock = std::chrono::steady_clock;

static const char request[] = "GET /big HTTP/1.1\r\n"
                              "Host: localhost\r\n"
                              "Connection: keep-alive\r\n"
                              "\r\n";

static void printRate(double seconds, uint64_t bytes) {
    double bps = static_cast<double>(bytes) / seconds;
    if (bps >= 1e9)
        printf("%.2f GB/s", bps / 1e9);
    else if (bps >= 1e6)
        printf("%.2f MB/s", bps / 1e6);
    else if (bps >= 1e3)
        printf("%.2f kB/s", bps / 1e3);
    else
        printf("%.0f B/s", bps);
}

int main(int argc, char* argv[]) {
    const char* host = argc > 1 ? argv[1] : "127.0.0.1";
    uint16_t port = argc > 2 ? static_cast<uint16_t>(atoi(argv[2])) : 8080;
    int durationSecs = argc > 3 ? atoi(argv[3]) : 10;

    printf("=== Throughput Client ===\n");
    printf("Target  : %s:%d\n", host, port);
    printf("Duration: %d seconds\n\n", durationSecs);

    ConnectArgs args{host, Port{port}, Milliseconds{3000}};
    auto result = SocketFactory::createTcpClient(AddressFamily::IPv4, args);
    if (!result) {
        printf("[error] connect failed: %s\n", result.message().c_str());
        return 1;
    }

    TcpSocket sock(std::move(result.value()));
    (void)sock.setNoDelay(true);
    (void)sock.setReceiveBufferSize(256 * 1024);
    (void)sock.setSendBufferSize(256 * 1024);

    assert(!sock.isBlocking()); // non-blocking is the default

    const size_t reqLen = sizeof(request) - 1;
    size_t sendOffset = 0; // position within current request being sent
    uint64_t totalBytes = 0;
    char buf[65536];

    auto startTime = Clock::now();
    auto testEnd = startTime + std::chrono::seconds(durationSecs);

    while (Clock::now() < testEnd) {
        // Send side: push as much of the current request as the kernel takes.
        int sent = sock.send(request + sendOffset, reqLen - sendOffset);
        if (sent > 0) {
            sendOffset += static_cast<size_t>(sent);
            if (sendOffset >= reqLen) sendOffset = 0;
        } else {
            auto err = sock.getLastError();
            if (err != SocketError::WouldBlock && err != SocketError::Timeout) {
                printf("[error] send failed\n");
                break;
            }
        }

        // Receive side: drain everything available right now.
        int n = sock.receive(buf, sizeof(buf));
        if (n > 0) {
            totalBytes += static_cast<uint64_t>(n);
        } else if (n == 0) {
            printf("[error] server closed connection\n");
            break;
        }
        // WouldBlock on receive is fine — just means nothing queued yet.
    }

    double elapsed
        = std::chrono::duration<double>(Clock::now() - startTime).count();

    printf("Bytes recv: %llu\n", (unsigned long long)totalBytes);
    printf("Elapsed   : %.2fs\n", elapsed);
    printf("Throughput: ");
    printRate(elapsed, totalBytes);
    printf("\n");

    return 0;
}
