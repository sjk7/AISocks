bool SocketImpl::waitReadable(std::chrono::milliseconds timeout) {
    RETURN_IF_INVALID();
    // Use the same OS-specific polling logic as connect()
    auto deadline = std::chrono::steady_clock::now() + timeout;

    for (;;) {
        auto sliceMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        if (sliceMs < 0) {
            setError(SocketError::Timeout, "waitReadable timed out");
            return false; // Timeout
        }

#ifdef __APPLE__
        // Use kqueue for waitReadable
        int evFd = ::kqueue();
        if (evFd == -1) return false;
        
        struct kevent reg{};
        EV_SET(&reg, static_cast<uintptr_t>(socketHandle), EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);
        if (::kevent(evFd, &reg, 1, nullptr, 0, nullptr) == -1) {
            ::close(evFd);
            return false;
        }
        
        struct timespec ts{};
        ts.tv_sec = sliceMs / 1000;
        ts.tv_nsec = (sliceMs % 1000) * 1000000;
        
        struct kevent out{};
        int nReady = ::kevent(evFd, nullptr, 0, &out, 1, &ts);
        ::close(evFd);
        
        if (nReady < 0) return false;
        if (nReady == 0) {
            setError(SocketError::Timeout, "waitReadable timed out");
            return false; // Timeout
        }
        return true;
        
#elif defined(__linux__)
        // Use epoll for waitReadable
        int evFd = ::epoll_create1(EPOLL_CLOEXEC);
        if (evFd == -1) return false;
        
        struct epoll_event epev{};
        epev.events = EPOLLIN | EPOLLERR;
        epev.data.fd = socketHandle;
        if (::epoll_ctl(evFd, EPOLL_CTL_ADD, socketHandle, &epev) == -1) {
            ::close(evFd);
            return false;
        }
        
        struct epoll_event outev{};
        int nReady = ::epoll_wait(evFd, &outev, 1, static_cast<int>(sliceMs));
        ::close(evFd);
        
        if (nReady < 0) return false;
        if (nReady == 0) {
            setError(SocketError::Timeout, "waitReadable timed out");
            return false; // Timeout
        }
        return true;
        
#elif defined(_WIN32)
        // Use WSAPoll for waitReadable
        WSAPOLLFD pfd{};
        pfd.fd = socketHandle;
        pfd.events = POLLIN;
        int nReady = ::WSAPoll(&pfd, 1, static_cast<int>(sliceMs));
        if (nReady < 0) return false;
        if (nReady == 0) {
            setError(SocketError::Timeout, "waitReadable timed out");
            return false; // Timeout
        }
        return true;
#endif
    }
}

bool SocketImpl::waitWritable(std::chrono::milliseconds timeout) {
    RETURN_IF_INVALID();
    // Use the same OS-specific polling logic as connect()
    auto deadline = std::chrono::steady_clock::now() + timeout;

    for (;;) {
        auto sliceMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        if (sliceMs < 0) {
            setError(SocketError::Timeout, "waitWritable timed out");
            return false; // Timeout
        }

#ifdef __APPLE__
        // Use kqueue for waitWritable
        int evFd = ::kqueue();
        if (evFd == -1) return false;
        
        struct kevent reg{};
        EV_SET(&reg, static_cast<uintptr_t>(socketHandle), EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, nullptr);
        if (::kevent(evFd, &reg, 1, nullptr, 0, nullptr) == -1) {
            ::close(evFd);
            return false;
        }
        
        struct timespec ts{};
        ts.tv_sec = sliceMs / 1000;
        ts.tv_nsec = (sliceMs % 1000) * 1000000;
        
        struct kevent out{};
        int nReady = ::kevent(evFd, nullptr, 0, &out, 1, &ts);
        ::close(evFd);
        
        if (nReady < 0) return false;
        if (nReady == 0) {
            setError(SocketError::Timeout, "waitWritable timed out");
            return false; // Timeout
        }
        return true;
        
#elif defined(__linux__)
        // Use epoll for waitWritable
        int evFd = ::epoll_create1(EPOLL_CLOEXEC);
        if (evFd == -1) return false;
        
        struct epoll_event epev{};
        epev.events = EPOLLOUT | EPOLLERR;
        epev.data.fd = socketHandle;
        if (::epoll_ctl(evFd, EPOLL_CTL_ADD, socketHandle, &epev) == -1) {
            ::close(evFd);
            return false;
        }
        
        struct epoll_event outev{};
        int nReady = ::epoll_wait(evFd, &outev, 1, static_cast<int>(sliceMs));
        ::close(evFd);
        
        if (nReady < 0) return false;
        if (nReady == 0) {
            setError(SocketError::Timeout, "waitWritable timed out");
            return false; // Timeout
        }
        return true;
        
#elif defined(_WIN32)
        // Use WSAPoll for waitWritable
        WSAPOLLFD pfd{};
        pfd.fd = socketHandle;
        pfd.events = POLLOUT;
        int nReady = ::WSAPoll(&pfd, 1, static_cast<int>(sliceMs));
        if (nReady < 0) return false;
        if (nReady == 0) {
            setError(SocketError::Timeout, "waitWritable timed out");
            return false; // Timeout
        }
        return true;
#endif
    }
}
