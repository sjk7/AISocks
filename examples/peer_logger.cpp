// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com 
// 
// peer_logger.cpp — demonstrates getPeerEndpoint() /
// getLocalEndpoint() as a lightweight logging helper for accepted and connected
// sockets.
//
// The pattern shown here is idiomatic for production logging:
//   1. After accept()  — log what peer connected and on which local address.
//   2. After connect() — confirm the remote end and local ephemeral port.
//   3. On error        — include endpoint context in the error message.
//
// Both functions delegate to getpeername(2) / getsockname(2) internally and
// return std::nullopt if the socket is invalid or not yet connected.

#include "Socket.h"
#include <cassert>
#include <iostream>
#include <string>
#include <thread>

using namespace aiSocks;

// ---------------------------------------------------------------------------
// logPeerInfo — call immediately after accept() or connect().
// Prints a one-liner with the full four-tuple useful for connection tracking.
// ---------------------------------------------------------------------------
static void logPeerInfo(const Socket& s, const std::string& role) {
    auto local = s.getLocalEndpoint(); // getsockname(2)
    auto peer = s.getPeerEndpoint(); // getpeername(2)

    std::cout << "[" << role << "] ";

    if (local) {
        std::cout << "local=" << local->toString();
    } else {
        std::cout << "local=<unknown>";
    }

    if (peer) {
        std::cout << "  peer=" << peer->toString();
    } else {
        std::cout << "  peer=<not connected>";
    }

    std::cout << "\n";
}

// ---------------------------------------------------------------------------
// logAcceptedPeer — server side: log each inbound connection.
// ---------------------------------------------------------------------------
static void logAcceptedPeer(const Socket& accepted) {
    logPeerInfo(accepted, "server-side accepted");
}

// ---------------------------------------------------------------------------
// A minimal TCP echo server that logs every peer on accept.
// Runs on the provided port, handles one connection, then exits.
// ---------------------------------------------------------------------------
static void runEchoServer(Port port) {
    Socket server(SocketType::TCP, AddressFamily::IPv4,
        ServerBind{.address = "127.0.0.1", .port = port, .backlog = 1});

    auto localEp = server.getLocalEndpoint();
    std::cout << "[server] listening on "
              << (localEp ? localEp->toString() : "?") << "\n";

    auto conn = server.accept();
    if (!conn) {
        std::cerr << "[server] accept failed: " << server.getErrorMessage()
                  << "\n";
        return;
    }

    // Log the four-tuple immediately — this is the central pattern.
    logAcceptedPeer(*conn);

    // Echo loop: read once, write back, done.
    char buf[256] = {};
    int r = conn->receive(buf, sizeof(buf) - 1);
    if (r > 0) {
        buf[r] = '\0';
        std::cout << "[server] echoing " << r << " byte(s): \"" << buf
                  << "\"\n";
        conn->send(buf, static_cast<size_t>(r));
    }
}

// ---------------------------------------------------------------------------
// A minimal TCP echo client that logs its own endpoint after connect.
// ---------------------------------------------------------------------------
static void runEchoClient(Port port) {
    Socket client(SocketType::TCP, AddressFamily::IPv4,
        ConnectTo{.address = "127.0.0.1", .port = port});

    // Log immediately after connect — shows the kernel-assigned ephemeral port.
    logPeerInfo(client, "client-side connected");

    const std::string msg = "hello from peer_logger";
    client.send(msg.data(), msg.size());

    char buf[256] = {};
    int r = client.receive(buf, sizeof(buf) - 1);
    if (r > 0) {
        buf[r] = '\0';
        std::cout << "[client] received echo: \"" << buf << "\"\n";
    }
}

// ---------------------------------------------------------------------------
// Demonstrate the logging helper with a UDP connected socket too.
// ---------------------------------------------------------------------------
static void runUdpPeerLog(Port port) {
    // Server side: just bind and receive one datagram.
    Socket server(SocketType::UDP, AddressFamily::IPv4);
    server.setReuseAddress(true);
    if (!server.bind("127.0.0.1", port)) {
        std::cerr << "[udp-server] bind failed\n";
        return;
    }

    // Client: connect() the UDP socket so send() works without explicit dest.
    Socket client(SocketType::UDP, AddressFamily::IPv4);
    if (!client.connect("127.0.0.1", port)) {
        std::cerr << "[udp-client] connect failed\n";
        return;
    }

    // getpeername() works on connected UDP sockets too.
    logPeerInfo(client, "udp-client connected");

    const char dgram[] = "udp-peer-log";
    client.send(dgram, sizeof(dgram) - 1);

    char buf[64] = {};
    Endpoint from;
    int r = server.receiveFrom(buf, sizeof(buf), from);
    if (r > 0) {
        std::cout << "[udp-server] datagram from " << from.toString() << ": \""
                  << std::string(buf, static_cast<size_t>(r)) << "\"\n";
    }
}

int main() {
    constexpr Port TCP_PORT{19900};
    constexpr Port UDP_PORT{19901};

    // ---- TCP echo with peer logging ----------------------------------------
    std::thread serverThread([&]() { runEchoServer(TCP_PORT); });
    // Give the server a moment to start listening.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    runEchoClient(TCP_PORT);
    serverThread.join();

    std::cout << "\n";

    // ---- UDP connected socket peer logging ----------------------------------
    runUdpPeerLog(UDP_PORT);

    return 0;
}
