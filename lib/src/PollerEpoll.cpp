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
    std::unordered_map<uintptr_t, const Socket*> sockets;
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
        throw SocketException(SocketError::CreateFailed, "epoll_create1()",
            "Failed to create epoll instance", errno, false);
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
    pImpl_->sockets[static_cast<uintptr_t>(fd)] = &s;
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

    pImpl_->sockets.erase(static_cast<uintptr_t>(fd));
    return true;
}

std::vector<PollResult> Poller::wait(Milliseconds timeout) {
    const int timeoutMs
        = (timeout.count() < 0) ? -1 : static_cast<int>(timeout.count());

    const int maxEvents = static_cast<int>(pImpl_->sockets.size()) + 1;
    std::vector<struct epoll_event> events(static_cast<size_t>(maxEvents));

    for (;;) {
        int n = ::epoll_wait(pImpl_->epfd, events.data(), maxEvents, timeoutMs);
        if (n < 0) {
            if (errno == EINTR)
                return {}; // signal received -- let caller check stop flag
            throw SocketException(SocketError::Unknown, "epoll_wait()",
                "epoll_wait() failed", errno, false);
        }

        std::vector<PollResult> results;
        results.reserve(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            const struct epoll_event& ev = events[static_cast<size_t>(i)];
            auto fd = static_cast<uintptr_t>(ev.data.fd);
            auto it = pImpl_->sockets.find(fd);
            if (it == pImpl_->sockets.end()) continue;

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
                // treat as readable so the read path sees n==0 and disconnects.
                bits |= static_cast<uint8_t>(PollEvent::Readable);
            }
            results.push_back({it->second, static_cast<PollEvent>(bits)});
        }
        return results;
    }
}

} // namespace aiSocks

#endif // __linux__
