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

#include <vector>
#include <cerrno>

namespace aiSocks {

struct Poller::Impl {
    int kq{-1};
    // Sparse arrays for O(1) fd->Socket lookup.
    std::vector<const Socket*> socketArray;
    std::vector<bool> socketValid;
    // Flat merge buffers: replace the per-wait() unordered_map.
    // mergeBits[fd] accumulates PollEvent flags across multiple kevent entries
    // for the same fd.  seenFds tracks which slots were written so we can
    // zero them without scanning the whole array.
    std::vector<uint8_t> mergeBits;
    std::vector<size_t> seenFds;
    // Reusable kevent output buffer — sized to 2 filters × registered fds.
    std::vector<struct kevent> keventBuf;
    // Reusable result buffer to avoid per-call allocation in wait().
    std::vector<PollResult> resultBuffer;

    void ensureCapacity(int fd) {
        size_t required = static_cast<size_t>(fd) + 1;
        if (socketArray.size() < required) {
            socketArray.resize(required, nullptr);
            socketValid.resize(required, false);
            mergeBits.resize(required, 0);
            keventBuf.resize(required * 2 + 1);
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
        // EV_DISABLE keeps the filter registered in the kernel so re-enabling
        // it later only needs EV_ENABLE, avoiding the EV_DELETE + EV_ADD
        // round-trip that would otherwise happen on every request cycle.
        EV_SET(&changes[n++], static_cast<uintptr_t>(fd), EVFILT_READ,
            EV_ADD | EV_DISABLE, 0, 0, nullptr);
    }
    if (hasFlag(interest, PollEvent::Writable)) {
        EV_SET(&changes[n++], static_cast<uintptr_t>(fd), EVFILT_WRITE,
            EV_ADD | EV_ENABLE, 0, 0, nullptr);
    } else {
        EV_SET(&changes[n++], static_cast<uintptr_t>(fd), EVFILT_WRITE,
            EV_ADD | EV_DISABLE, 0, 0, nullptr);
    }
    // kevent() returns ENOENT for filters not yet registered — benign.
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
        int64_t effectiveTimeout = timeout.count;
        if (effectiveTimeout == 0) {
            // Clamp 0ms to 1ms: a true zero-timeout busy-spin burns a whole
            // CPU core and starves the TCP stack under load.
            ts.tv_sec = 0;
            ts.tv_nsec = 1000000L; // 1 ms
        } else {
            ts.tv_sec = static_cast<time_t>(effectiveTimeout / 1000);
            ts.tv_nsec
                = static_cast<long>((effectiveTimeout % 1000) * 1000000L);
        }
        tsp = &ts;
    }

    // keventBuf is pre-sized in ensureCapacity; guard against empty state.
    if (pImpl_->keventBuf.empty()) pImpl_->keventBuf.resize(1);
    const int maxEvents = static_cast<int>(pImpl_->keventBuf.size());

    for (;;) {
        int n = ::kevent(
            pImpl_->kq, nullptr, 0, pImpl_->keventBuf.data(), maxEvents, tsp);
        if (n < 0) {
            if (errno == EINTR)
                return {}; // signal received -- let caller check stop flag
            return {};
        }

        // Merge per-filter events for the same fd into one PollResult.
        // Use a flat byte array indexed by fd — no hash map, no allocation.
        // seenFds tracks which slots were written so we reset only those.
        auto& arr = pImpl_->socketArray;
        pImpl_->seenFds.clear();
        for (int i = 0; i < n; ++i) {
            const struct kevent& ev = pImpl_->keventBuf[static_cast<size_t>(i)];
            auto fd = static_cast<size_t>(ev.ident);
            if (fd >= arr.size()) continue;
            uint8_t bits = pImpl_->mergeBits[fd];
            if (bits == 0) pImpl_->seenFds.push_back(fd);
            if ((ev.flags & EV_ERROR) != 0) {
                bits |= static_cast<uint8_t>(PollEvent::Error);
            } else if (ev.filter == EVFILT_READ) {
                bits |= static_cast<uint8_t>(PollEvent::Readable);
                // EV_EOF (peer closed): still signal readable so caller drains.
                if ((ev.flags & EV_EOF) != 0)
                    bits |= static_cast<uint8_t>(PollEvent::Readable);
            } else if (ev.filter == EVFILT_WRITE) {
                // EV_EOF on write filter: peer shut down their read side.
                // Signal Writable; onReadable will detect full close via n==0.
                bits |= static_cast<uint8_t>(PollEvent::Writable);
            }
            pImpl_->mergeBits[fd] = bits;
        }

        pImpl_->resultBuffer.clear();
        pImpl_->resultBuffer.reserve(pImpl_->seenFds.size());
        for (auto fd : pImpl_->seenFds) {
            const uint8_t bits = pImpl_->mergeBits[fd];
            pImpl_->mergeBits[fd] = 0; // reset for next call
            if (pImpl_->socketValid[fd] && arr[fd])
                pImpl_->resultBuffer.push_back(
                    {arr[fd], static_cast<PollEvent>(bits)});
        }
        return pImpl_->resultBuffer;
    }
}

} // namespace aiSocks

#endif // defined(__APPLE__) || defined(__FreeBSD__)
