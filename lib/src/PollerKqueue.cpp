// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// PollerKqueue.cpp  kqueue backend for macOS / BSD.
// Compiled only when CMAKE detects Apple or FreeBSD.

#if defined(__APPLE__) || defined(__FreeBSD__)

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
    // Sparse array for O(1) direct fd->Socket lookup instead of unordered_map
    std::vector<const Socket*> socketArray;
    std::vector<bool> socketValid;
    // Reusable result buffer to avoid per-call allocation in wait()
    std::vector<PollResult> resultBuffer;

    // Helper to ensure array is large enough for fd
    void ensureCapacity(int fd) {
        size_t required = static_cast<size_t>(fd) + 1;
        if (socketArray.size() < required) {
            socketArray.resize(required, nullptr);
            socketValid.resize(required, false);
        }
    }
};

Poller::Poller() : pImpl_(std::make_unique<Impl>()) {
    pImpl_->kq = ::kqueue();
    if (pImpl_->kq == -1) {
        // Don't throw - set to invalid state
        pImpl_->kq = -1;
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
    pImpl_->ensureCapacity(fd);
    pImpl_->socketArray[fd] = &s;
    pImpl_->socketValid[fd] = true;
    return true;
}

bool Poller::modify(const Socket& s, PollEvent interest) {
    // Update both filters atomically with a single kevent() call using
    // EV_ADD (which acts as modify when already registered) and EV_DELETE
    // for filters that are no longer wanted.  This avoids the removeadd
    // gap during which an event on the fd could be lost.
    auto fd = static_cast<int>(s.getNativeHandle());
    if (fd == -1) return false;

    struct kevent changes[2];
    int n = 0;
    // Add/enable desired filters.
    if (hasFlag(interest, PollEvent::Readable)) {
        EV_SET(&changes[n++], static_cast<uintptr_t>(fd), EVFILT_READ,
            EV_ADD | EV_ENABLE, 0, 0, nullptr);
    } else {
        EV_SET(&changes[n++], static_cast<uintptr_t>(fd), EVFILT_READ,
            EV_DELETE, 0, 0, nullptr);
    }
    if (hasFlag(interest, PollEvent::Writable)) {
        EV_SET(&changes[n++], static_cast<uintptr_t>(fd), EVFILT_WRITE,
            EV_ADD | EV_ENABLE, 0, 0, nullptr);
    } else {
        EV_SET(&changes[n++], static_cast<uintptr_t>(fd), EVFILT_WRITE,
            EV_DELETE, 0, 0, nullptr);
    }
    // kevent() returns ENOENT for filters that were not registered  benign.
    ::kevent(pImpl_->kq, changes, n, nullptr, 0, nullptr);
    pImpl_->ensureCapacity(fd);
    pImpl_->socketArray[fd] = &s;
    pImpl_->socketValid[fd] = true;
    return true;
}

bool Poller::remove(const Socket& s) {
    auto fd = static_cast<int>(s.getNativeHandle());
    if (fd == -1) return false;

    struct kevent changes[2];
    EV_SET(&changes[0], static_cast<uintptr_t>(fd), EVFILT_READ, EV_DELETE, 0,
        0, nullptr);
    EV_SET(&changes[1], static_cast<uintptr_t>(fd), EVFILT_WRITE, EV_DELETE, 0,
        0, nullptr);
    // Ignore errors  filters that weren't registered return ENOENT.
    ::kevent(pImpl_->kq, changes, 2, nullptr, 0, nullptr);

    if (fd < static_cast<int>(pImpl_->socketArray.size())) {
        pImpl_->socketArray[fd] = nullptr;
        pImpl_->socketValid[fd] = false;
    }
    return true;
}

std::vector<PollResult> Poller::wait(Milliseconds timeout) {
    // Convert timeout: -1 means block forever (nullptr timespec).
    struct timespec ts{};
    struct timespec* tsp = nullptr;
    if (timeout.count >= 0) {
        ts.tv_sec = static_cast<time_t>(timeout.count / 1000);
        ts.tv_nsec = static_cast<long>((timeout.count % 1000) * 1000000L);
        tsp = &ts;
    }

    // Allocate enough room for two events per registered socket (READ+WRITE).
    const int maxEvents = static_cast<int>(pImpl_->socketArray.size() * 2) + 1;
    std::vector<struct kevent> events(static_cast<size_t>(maxEvents));

    for (;;) {
        int n = ::kevent(pImpl_->kq, nullptr, 0, events.data(), maxEvents, tsp);
        if (n < 0) {
            if (errno == EINTR)
                return {}; // signal received -- let caller check stop flag
            // Don't throw - return empty result on error
            return {};
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
                // EV_EOF on a write filter means the peer shut down their read
                // side, but we may still have data to send and the read side
                // may still be open.  Just signal Writable and let onReadable
                // detect the full close via a 0-byte read.
                bits |= static_cast<uint8_t>(PollEvent::Writable);
            }
            ready[fd] = static_cast<PollEvent>(bits);
        }

        pImpl_->resultBuffer.clear();
        pImpl_->resultBuffer.reserve(ready.size());
        auto& results = pImpl_->resultBuffer;
        for (const auto& kv : ready) {
            auto fd = static_cast<int>(kv.first);
            if (fd < static_cast<int>(pImpl_->socketArray.size())
                && pImpl_->socketValid[fd] && pImpl_->socketArray[fd]) {
                results.push_back({pImpl_->socketArray[fd], kv.second});
            }
        }
        return results;
    }
}

} // namespace aiSocks

#endif // defined(__APPLE__) || defined(__FreeBSD__)
