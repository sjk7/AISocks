// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// PollerWSAPoll.cpp  WSAPoll backend for Windows.
// Compiled only when CMAKE detects WIN32.

#include "Poller.h"
#include "SocketImpl.h"

#include <winsock2.h>
#include <vector>
#include <algorithm>

namespace aiSocks {

struct Poller::Impl {
    // Parallel vectors: fds[i] and sockets[i] refer to the same entry.
    std::vector<WSAPOLLFD> fds;
    std::vector<const Socket*> sockets;
};

Poller::Poller() : pImpl_(std::make_unique<Impl>()) {
    // WSAPoll is available from Vista+; no extra init needed.
}

Poller::~Poller() = default;

bool Poller::add(const Socket& s, PollEvent interest) {
    auto handle = s.getNativeHandle();
    if (handle == static_cast<uintptr_t>(-1)) return false;

    WSAPOLLFD pfd{};
    pfd.fd = static_cast<SOCKET>(handle);
    pfd.events = 0;
    if (hasFlag(interest, PollEvent::Readable)) pfd.events |= POLLRDNORM;
    if (hasFlag(interest, PollEvent::Writable)) pfd.events |= POLLWRNORM;

    pImpl_->fds.push_back(pfd);
    pImpl_->sockets.push_back(&s);
    return true;
}

bool Poller::modify(const Socket& s, PollEvent interest) {
    auto handle = s.getNativeHandle();
    if (handle == static_cast<uintptr_t>(-1)) return false;
    auto sock = static_cast<SOCKET>(handle);

    for (size_t i = 0; i < pImpl_->fds.size(); ++i) {
        if (pImpl_->fds[i].fd == sock) {
            pImpl_->fds[i].events = 0;
            if (hasFlag(interest, PollEvent::Readable))
                pImpl_->fds[i].events |= POLLRDNORM;
            if (hasFlag(interest, PollEvent::Writable))
                pImpl_->fds[i].events |= POLLWRNORM;
            return true;
        }
    }
    return false; // not found
}

bool Poller::remove(const Socket& s) {
    auto handle = s.getNativeHandle();
    if (handle == static_cast<uintptr_t>(-1)) return false;
    auto sock = static_cast<SOCKET>(handle);

    for (size_t i = 0; i < pImpl_->fds.size(); ++i) {
        if (pImpl_->fds[i].fd == sock) {
            pImpl_->fds.erase(pImpl_->fds.begin() + static_cast<ptrdiff_t>(i));
            pImpl_->sockets.erase(
                pImpl_->sockets.begin() + static_cast<ptrdiff_t>(i));
            return true;
        }
    }
    return true; // not found  benign
}

std::vector<PollResult> Poller::wait(Milliseconds timeout) {
    if (pImpl_->fds.empty()) return {};

    // On Windows, std::signal handlers fire on a separate thread, so WSAPoll
    // with an infinite timeout will never be interrupted. Cap the wait at 100ms
    // so the run() loop can check the stop flag and exit cleanly.
    static constexpr int MAX_WAIT_MS = 100;
    const int timeoutMs = (timeout.count() < 0)
        ? MAX_WAIT_MS
        : static_cast<int>(timeout.count());

    // WSAPoll modifies revents in-place; clear them first.
    for (auto& pfd : pImpl_->fds) {
        pfd.revents = 0;
    }

    int rc = ::WSAPoll(
        pImpl_->fds.data(), static_cast<ULONG>(pImpl_->fds.size()), timeoutMs);
    if (rc == SOCKET_ERROR) {
        throw SocketException(SocketError::Unknown, "WSAPoll()",
            "WSAPoll() failed", WSAGetLastError(), false);
    }

    std::vector<PollResult> results;
    for (size_t i = 0; i < pImpl_->fds.size(); ++i) {
        SHORT rev = pImpl_->fds[i].revents;
        if (rev == 0) continue;

        uint8_t bits = 0;
        if ((rev & (POLLRDNORM | POLLIN)) != 0)
            bits |= static_cast<uint8_t>(PollEvent::Readable);
        if ((rev & (POLLWRNORM | POLLOUT)) != 0)
            bits |= static_cast<uint8_t>(PollEvent::Writable);
        if ((rev & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            bits |= static_cast<uint8_t>(PollEvent::Error);
            bits |= static_cast<uint8_t>(PollEvent::Readable);
        }
        if (bits != 0)
            results.push_back(
                {pImpl_->sockets[i], static_cast<PollEvent>(bits)});
    }
    return results;
}

} // namespace aiSocks
