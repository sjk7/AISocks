// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// PollerKqueue.cpp — kqueue backend for macOS / BSD.
// Compiled only when CMAKE detects Apple or FreeBSD.

#include "Poller.h"
#include "SocketImpl.h"

#include <sys/event.h>
#include <sys/time.h>
#include <unistd.h>

#include <unordered_map>
#include <vector>
#include <cerrno>

namespace aiSocks {

struct Poller::Impl {
    int kq{-1};
    // Maps raw fd → borrowed Socket pointer (never owned).
    std::unordered_map<uintptr_t, const Socket*> sockets;
};

Poller::Poller() : pImpl_(std::make_unique<Impl>()) {
    pImpl_->kq = ::kqueue();
    if (pImpl_->kq == -1) {
        throw SocketException(SocketError::CreateFailed, "kqueue()",
            "Failed to create kqueue event queue", errno, false);
    }
}

Poller::~Poller() {
    if (pImpl_ && pImpl_->kq != -1) {
        ::close(pImpl_->kq);
        pImpl_->kq = -1;
    }
}

bool Poller::add(const Socket& s, PollEvent interest) {
    auto fd = static_cast<int>(s.getNativeHandle());
    if (fd == -1) return false;

    struct kevent changes[2];
    int n = 0;
    if (hasFlag(interest, PollEvent::Readable)) {
        EV_SET(&changes[n++], static_cast<uintptr_t>(fd), EVFILT_READ,
            EV_ADD | EV_ENABLE, 0, 0, nullptr);
    }
    if (hasFlag(interest, PollEvent::Writable)) {
        EV_SET(&changes[n++], static_cast<uintptr_t>(fd), EVFILT_WRITE,
            EV_ADD | EV_ENABLE, 0, 0, nullptr);
    }
    if (n == 0) return true;

    if (::kevent(pImpl_->kq, changes, n, nullptr, 0, nullptr) == -1) {
        return false;
    }
    pImpl_->sockets[static_cast<uintptr_t>(fd)] = &s;
    return true;
}

bool Poller::modify(const Socket& s, PollEvent interest) {
    // Remove both filters unconditionally, then re-add with new interest.
    remove(s);
    return add(s, interest);
}

bool Poller::remove(const Socket& s) {
    auto fd = static_cast<int>(s.getNativeHandle());
    if (fd == -1) return false;

    struct kevent changes[2];
    EV_SET(&changes[0], static_cast<uintptr_t>(fd), EVFILT_READ, EV_DELETE, 0,
        0, nullptr);
    EV_SET(&changes[1], static_cast<uintptr_t>(fd), EVFILT_WRITE, EV_DELETE, 0,
        0, nullptr);
    // Ignore errors — filters that weren't registered return ENOENT.
    ::kevent(pImpl_->kq, changes, 2, nullptr, 0, nullptr);

    pImpl_->sockets.erase(static_cast<uintptr_t>(fd));
    return true;
}

std::vector<PollResult> Poller::wait(Milliseconds timeout) {
    // Convert timeout: -1 means block forever (nullptr timespec).
    struct timespec ts{};
    struct timespec* tsp = nullptr;
    if (timeout.count() >= 0) {
        ts.tv_sec = static_cast<time_t>(timeout.count() / 1000);
        ts.tv_nsec = static_cast<long>((timeout.count() % 1000) * 1000000L);
        tsp = &ts;
    }

    // Allocate enough room for two events per registered socket (READ+WRITE).
    auto& sockets = pImpl_->sockets;
    const int maxEvents = static_cast<int>(sockets.size() * 2) + 1;
    std::vector<struct kevent> events(static_cast<size_t>(maxEvents));

    for (;;) {
        int n = ::kevent(pImpl_->kq, nullptr, 0, events.data(), maxEvents, tsp);
        if (n < 0) {
            if (errno == EINTR) continue;
            throw SocketException(SocketError::Unknown, "kevent()",
                "kevent() wait failed", errno, false);
        }

        // Merge per-filter events for the same fd into one PollResult.
        std::unordered_map<uintptr_t, PollEvent> ready;
        for (int i = 0; i < n; ++i) {
            const struct kevent& ev = events[static_cast<size_t>(i)];
            uintptr_t fd = ev.ident;
            auto bits = static_cast<uint8_t>(ready[fd]); // default-inits to 0

            if ((ev.flags & EV_ERROR) != 0) {
                bits |= static_cast<uint8_t>(PollEvent::Error);
            } else if (ev.filter == EVFILT_READ) {
                bits |= static_cast<uint8_t>(PollEvent::Readable);
                // EV_EOF (peer closed): still signal readable so caller drains.
                if ((ev.flags & EV_EOF) != 0)
                    bits |= static_cast<uint8_t>(PollEvent::Readable);
            } else if (ev.filter == EVFILT_WRITE) {
                // EV_EOF on a write filter: the peer has shut down their read
                // side; flag as both Writable (buffer still available) and
                // Error so callers detect the half-close.
                if ((ev.flags & EV_EOF) != 0)
                    bits |= static_cast<uint8_t>(PollEvent::Error);
                bits |= static_cast<uint8_t>(PollEvent::Writable);
            }
            ready[fd] = static_cast<PollEvent>(bits);
        }

        std::vector<PollResult> results;
        results.reserve(ready.size());
        for (const auto& kv : ready) {
            auto it = sockets.find(kv.first);
            if (it != sockets.end()) {
                results.push_back({it->second, kv.second});
            }
        }
        return results;
    }
}

} // namespace aiSocks
