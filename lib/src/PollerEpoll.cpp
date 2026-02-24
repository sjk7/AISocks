// This is a personal academic project. Dear PVS-Studio, please check it.
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

Poller::Poller() : pImpl_(std::make_unique<Impl>()) {
    pImpl_->epfd = ::epoll_create1(EPOLL_CLOEXEC);
    if (pImpl_->epfd == -1) {
        // Don't throw - set to invalid state
        // Users can check isValid() via the Poller methods
        pImpl_->epfd = -1;
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

std::vector<PollResult> Poller::wait(Milliseconds timeout) {
    const int timeoutMs
        = (timeout.count < 0) ? -1 : static_cast<int>(timeout.count);

    const int maxEvents = static_cast<int>(pImpl_->socketArray.size()) + 1;
    std::vector<struct epoll_event> events(static_cast<size_t>(maxEvents));

    for (;;) {
        int n = ::epoll_wait(pImpl_->epfd, events.data(), maxEvents, timeoutMs);
        if (n < 0) {
            if (errno == EINTR)
                return {}; // signal received -- let caller check stop flag
            // Don't throw - return empty result on error
            // Users can check system error via errno
            return {};
        }

        pImpl_->resultBuffer.clear();
        pImpl_->resultBuffer.reserve(static_cast<size_t>(n));
        auto& results = pImpl_->resultBuffer;
        for (int i = 0; i < n; ++i) {
            const struct epoll_event& ev = events[static_cast<size_t>(i)];
            auto fd = static_cast<int>(ev.data.fd);
            if (fd < static_cast<int>(pImpl_->socketArray.size())
                && pImpl_->socketValid[fd] && pImpl_->socketArray[fd]) {

                uint8_t bits = 0;
                if ((ev.events & (EPOLLIN | EPOLLRDNORM | EPOLLRDHUP)) != 0)
                    bits |= static_cast<uint8_t>(PollEvent::Readable);
                if ((ev.events & (EPOLLOUT | EPOLLWRNORM)) != 0)
                    bits |= static_cast<uint8_t>(PollEvent::Writable);
                if ((ev.events & EPOLLERR) != 0) {
                    bits |= static_cast<uint8_t>(PollEvent::Error);
                    // Let caller drain to detect the cause.
                    bits |= static_cast<uint8_t>(PollEvent::Readable);
                }
                if ((ev.events & EPOLLHUP) != 0) {
                    // EPOLLHUP = remote closed (TCP FIN) -- not a socket error,
                    // treat as readable so the read path sees n==0 and
                    // disconnects.
                    bits |= static_cast<uint8_t>(PollEvent::Readable);
                }
                results.push_back(
                    {pImpl_->socketArray[fd], static_cast<PollEvent>(bits)});
            }
        }
        return results;
    }
}

} // namespace aiSocks

#endif // __linux__
