// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// PollerEpoll.cpp  epoll backend for Linux.
// Compiled only when CMAKE detects Linux.

#ifdef __linux__

#include "Poller.h"
#include "SocketImpl.h"

#include <sys/epoll.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <climits>
#include <unordered_map>
#include <vector>

namespace aiSocks {

struct Poller::Impl {
    int epfd{-1};
    // Sparse array for O(1) direct fd->Socket lookup instead of unordered_map
    std::vector<const Socket*> socketArray;
    std::vector<bool> socketValid;
    // Reusable result buffer to avoid per-call allocation in wait()
    std::vector<PollResult> resultBuffer;
    // Reusable epoll_event inbox buffer — grown when the registered set grows,
    // never shrunk, so epoll_wait() never allocates on the hot path.
    std::vector<struct epoll_event> eventsBuffer;

    // Helper to ensure array is large enough for fd
    void ensureCapacity(int fd) {
        size_t required = static_cast<size_t>(fd) + 1;
        if (socketArray.size() < required) {
            socketArray.resize(required, nullptr);
            socketValid.resize(required, false);
        }
    }
};

static uint32_t interestToEpollEvents(PollEvent interest) {
    uint32_t ev = 0;
    if (hasFlag(interest, PollEvent::Readable)) ev |= EPOLLIN | EPOLLRDHUP;
    if (hasFlag(interest, PollEvent::Writable)) ev |= EPOLLOUT;
    return ev;
}

// Returns the epoll_wait() timeout in milliseconds.
//   == 0  → -1 (block forever: epoll_wait with -1 waits indefinitely)
//   < 0   → 1ms minimum (avoid busy-spin)
//   > 0   → value clamped to INT_MAX
static int toEpollTimeout_(Milliseconds timeout) {
    int64_t ms = timeout.count;
    if (ms == 0) return -1; // block forever
    if (ms < 0) return 1;
    return static_cast<int>(std::min(ms, static_cast<int64_t>(INT_MAX)));
}

// Translates raw epoll event flags into the platform-neutral PollEvent mask.
static uint8_t translateEpollBits_(uint32_t ev) {
    uint8_t bits = 0;
    if ((ev & (EPOLLIN | EPOLLRDNORM | EPOLLRDHUP)) != 0)
        bits |= static_cast<uint8_t>(PollEvent::Readable);
    if ((ev & (EPOLLOUT | EPOLLWRNORM)) != 0)
        bits |= static_cast<uint8_t>(PollEvent::Writable);
    if ((ev & EPOLLERR) != 0) {
        bits |= static_cast<uint8_t>(PollEvent::Error);
        bits |= static_cast<uint8_t>(PollEvent::Readable); // let caller drain
    }
    if ((ev & EPOLLHUP) != 0)
        // EPOLLHUP = remote closed (TCP FIN) -- treat as readable so the
        // read path sees n==0 and disconnects cleanly.
        bits |= static_cast<uint8_t>(PollEvent::Readable);
    return bits;
}

Poller::Poller() : pImpl_(std::make_unique<Impl>()) {
    pImpl_->epfd = ::epoll_create1(EPOLL_CLOEXEC);
    if (pImpl_->epfd == -1) {
        // epoll_create1() failed; wait() will return empty results.
    }
}

Poller::~Poller() {
    if (pImpl_ && pImpl_->epfd != -1) {
        ::close(pImpl_->epfd);
        pImpl_->epfd = -1;
    }
}

bool Poller::add(const Socket& s, PollEvent interest) {
    auto fd = static_cast<int>(s.getNativeHandle());
    if (fd == -1) return false;

    struct epoll_event ev{};
    ev.events = interestToEpollEvents(interest);
    ev.data.fd = fd;

    if (::epoll_ctl(pImpl_->epfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
        return false;
    }
    pImpl_->ensureCapacity(fd);
    pImpl_->socketArray[fd] = &s;
    pImpl_->socketValid[fd] = true;
    return true;
}

bool Poller::modify(const Socket& s, PollEvent interest) {
    auto fd = static_cast<int>(s.getNativeHandle());
    if (fd == -1) return false;

    struct epoll_event ev{};
    ev.events = interestToEpollEvents(interest);
    ev.data.fd = fd;

    if (::epoll_ctl(pImpl_->epfd, EPOLL_CTL_MOD, fd, &ev) == -1) {
        return false;
    }
    return true;
}

bool Poller::remove(const Socket& s) {
    auto fd = static_cast<int>(s.getNativeHandle());
    if (fd == -1) return false;

    struct epoll_event ev{}; // required on kernels < 2.6.9
    ::epoll_ctl(pImpl_->epfd, EPOLL_CTL_DEL, fd, &ev);

    if (fd < static_cast<int>(pImpl_->socketArray.size())) {
        pImpl_->socketArray[fd] = nullptr;
        pImpl_->socketValid[fd] = false;
    }
    return true;
}

const std::vector<PollResult>& Poller::wait(Milliseconds timeout) {
    const int timeoutMs = toEpollTimeout_(timeout);

    const int maxEvents = static_cast<int>(pImpl_->socketArray.size()) + 1;
    // Grow the persistent buffer only when the registered fd set has grown;
    // never shrink it so the common case (steady-state) is allocation-free.
    if (static_cast<int>(pImpl_->eventsBuffer.size()) < maxEvents)
        pImpl_->eventsBuffer.resize(static_cast<size_t>(maxEvents));
    auto& events = pImpl_->eventsBuffer;

    for (;;) {
        int n = ::epoll_wait(pImpl_->epfd, events.data(), maxEvents, timeoutMs);
        if (n < 0) {
            pImpl_->resultBuffer.clear();
            return pImpl_->resultBuffer; // EINTR or hard error; let caller
                                         // check stop flag
        }

        pImpl_->resultBuffer.clear();
        pImpl_->resultBuffer.reserve(static_cast<size_t>(n));
        auto& results = pImpl_->resultBuffer;
        for (int i = 0; i < n; ++i) {
            const struct epoll_event& ev = events[static_cast<size_t>(i)];
            auto fd = static_cast<int>(ev.data.fd);
            if (fd < static_cast<int>(pImpl_->socketArray.size())
                && pImpl_->socketValid[fd] && pImpl_->socketArray[fd]) {

                const uint8_t bits = translateEpollBits_(ev.events);
                results.push_back(
                    {pImpl_->socketArray[fd], static_cast<PollEvent>(bits)});
            }
        }
        return results;
    }
}

} // namespace aiSocks

#endif // __linux__
