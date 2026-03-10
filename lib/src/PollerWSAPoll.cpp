// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// PollerWSAPoll.cpp  WSAPoll backend for Windows.
// Compiled only when CMAKE detects WIN32.

#ifdef _WIN32

#include "pch.h"
#include "Poller.h"
#include "SocketImpl.h"

#include <winsock2.h>
#include <unordered_map>
#include <vector>

namespace aiSocks {

struct Poller::Impl {
    // Parallel vectors: fds[i] and sockets[i] refer to the same entry.
    std::vector<WSAPOLLFD> fds;
    std::vector<const Socket*> sockets;
    // O(1) SOCKET → vector-index map; kept in sync with fds/sockets.
    std::unordered_map<SOCKET, size_t> index;
    // Reusable result buffer to avoid per-call allocation in wait()
    std::vector<PollResult> resultBuffer;
};

Poller::Poller() : pImpl_(std::make_unique<Impl>()) {
    // WSAPoll is available from Vista+; no extra init needed.
}

Poller::~Poller() = default;

bool Poller::add(const Socket& s, PollEvent interest) {
    auto handle = s.getNativeHandle();
    if (handle == static_cast<uintptr_t>(-1)) return false;
    auto sock = static_cast<SOCKET>(handle);

    WSAPOLLFD pfd{};
    pfd.fd = sock;
    pfd.events = 0;
    if (hasFlag(interest, PollEvent::Readable)) pfd.events |= POLLRDNORM;
    if (hasFlag(interest, PollEvent::Writable)) pfd.events |= POLLWRNORM;

    pImpl_->index[sock] = pImpl_->fds.size();
    pImpl_->fds.push_back(pfd);
    pImpl_->sockets.push_back(&s);
    return true;
}

bool Poller::modify(const Socket& s, PollEvent interest) {
    auto handle = s.getNativeHandle();
    if (handle == static_cast<uintptr_t>(-1)) return false;
    auto sock = static_cast<SOCKET>(handle);

    auto it = pImpl_->index.find(sock);
    if (it == pImpl_->index.end()) return false; // not found

    WSAPOLLFD& pfd = pImpl_->fds[it->second];
    pfd.events = 0;
    if (hasFlag(interest, PollEvent::Readable)) pfd.events |= POLLRDNORM;
    if (hasFlag(interest, PollEvent::Writable)) pfd.events |= POLLWRNORM;
    return true;
}

bool Poller::remove(const Socket& s) {
    auto handle = s.getNativeHandle();
    if (handle == static_cast<uintptr_t>(-1)) return false;
    auto sock = static_cast<SOCKET>(handle);

    auto it = pImpl_->index.find(sock);
    if (it == pImpl_->index.end()) return true; // not found — benign

    const size_t i = it->second;
    const size_t last = pImpl_->fds.size() - 1;

    if (i != last) {
        // Swap with the last entry so we can pop_back in O(1).
        pImpl_->fds[i]    = pImpl_->fds[last];
        pImpl_->sockets[i] = pImpl_->sockets[last];
        // Update the index for the element that was moved.
        pImpl_->index[pImpl_->fds[i].fd] = i;
    }

    pImpl_->fds.pop_back();
    pImpl_->sockets.pop_back();
    pImpl_->index.erase(it);
    return true;
}

// Returns the WSAPoll() timeout in milliseconds.
//   == 0  → -1 (block forever: WSAPoll with -1 waits indefinitely)
//   < 0   → 1ms minimum (avoid busy-spin)
//   > 0   → value capped at 100ms
// The 100ms cap ensures run() can check the stop flag even for long
// timeouts.  On Windows, std::signal handlers fire on a separate thread,
// so WSAPoll must return frequently enough for the stop flag to be observed.
static int toWSAPollTimeout_(Milliseconds timeout) {
    int64_t ms = timeout.count;
    if (ms == 0) return -1; // block forever
    if (ms < 0) ms = 1;
    static constexpr int MAX_WAIT_MS = 100;
    return (ms > MAX_WAIT_MS) ? MAX_WAIT_MS : static_cast<int>(ms);
}

// Translates raw WSAPoll revents flags into the platform-neutral PollEvent
// mask.
static uint8_t translateWSAPollBits_(SHORT rev) {
    uint8_t bits = 0;
    if ((rev & (POLLRDNORM | POLLIN)) != 0)
        bits |= static_cast<uint8_t>(PollEvent::Readable);
    if ((rev & (POLLWRNORM | POLLOUT)) != 0)
        bits |= static_cast<uint8_t>(PollEvent::Writable);
    // POLLHUP = remote closed cleanly (TCP FIN) -- treat as readable so
    // the normal recv()==0 path handles the disconnect gracefully.
    if ((rev & POLLHUP) != 0) bits |= static_cast<uint8_t>(PollEvent::Readable);
    // POLLERR can fire with SO_ERROR==0 for connection resets on Windows --
    // treat as readable so recv() surfaces the actual condition rather than
    // triggering a spurious onError(code=0) log.
    if ((rev & POLLERR) != 0) bits |= static_cast<uint8_t>(PollEvent::Readable);
    // POLLNVAL is a genuine programming error (invalid fd).
    if ((rev & POLLNVAL) != 0) bits |= static_cast<uint8_t>(PollEvent::Error);
    return bits;
}

std::vector<PollResult> Poller::wait(Milliseconds timeout) {
    if (pImpl_->fds.empty()) return {};

    const int timeoutMs = toWSAPollTimeout_(timeout);

    // WSAPoll modifies revents in-place; clear them first.
    for (auto& pfd : pImpl_->fds) {
        pfd.revents = 0;
    }

    int rc = ::WSAPoll(
        pImpl_->fds.data(), static_cast<ULONG>(pImpl_->fds.size()), timeoutMs);
    if (rc == SOCKET_ERROR) {
        // Don't throw - return empty result on error
        // Users can check error via WSAGetLastError()
        return {};
    }

    pImpl_->resultBuffer.clear();
    pImpl_->resultBuffer.reserve(pImpl_->fds.size());
    auto& results = pImpl_->resultBuffer;
    for (size_t i = 0; i < pImpl_->fds.size(); ++i) {
        SHORT rev = pImpl_->fds[i].revents;
        if (rev == 0) continue;

        const uint8_t bits = translateWSAPollBits_(rev);
        if (bits != 0)
            results.push_back(
                {pImpl_->sockets[i], static_cast<PollEvent>(bits)});
    }
    return results;
}

} // namespace aiSocks

#endif // _WIN32
