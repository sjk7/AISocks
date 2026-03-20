// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Socket implementation: Data transfer and polling

#ifdef _WIN32
#include "pch.h"
#endif
#include "SocketImpl.h"
#include "SocketImplHelpers.h"
#include <chrono>
#include <cstring>
#include <algorithm>

#if defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/event.h>
#elif defined(__linux__)
#include <sys/epoll.h>
#endif

namespace aiSocks {

int SocketImpl::doTransfer_(bool forSend, void* buffer, const void* data,
    size_t length, const char* timeoutMsg, const char* failMsg) {
    if (!isValid()) {
        setError(SocketError::InvalidSocket, "Socket is not valid");
        return -1;
    }

    for (;;) {
        long long ioResult = SOCKET_ERROR_CODE;
        if (forSend) {
#ifdef _WIN32
            ioResult = ::send(socketHandle, static_cast<const char*>(data), static_cast<int>(length), 0);
#elif defined(MSG_NOSIGNAL)
            ioResult = ::send(socketHandle, data, length, MSG_NOSIGNAL);
#else
            ioResult = ::send(socketHandle, data, length, 0);
#endif
        } else {
#ifdef _WIN32
            ioResult = ::recv(socketHandle, static_cast<char*>(buffer), static_cast<int>(length), 0);
#else
            ioResult = ::recv(socketHandle, buffer, length, 0);
#endif
        }

        if (ioResult != SOCKET_ERROR_CODE) {
            lastError = SocketError::None;
            return static_cast<int>(ioResult);
        }

        int sysErr = getLastSystemError();
#ifndef _WIN32
        if (sysErr == EINTR) continue; 
#endif
        switch (classifyTransferSysError(sysErr)) {
            case SocketError::WouldBlock:
                setError(SocketError::WouldBlock, "Operation would block");
                return -1;
            case SocketError::Timeout:
                setError(SocketError::Timeout, timeoutMsg);
                return -1;
            case SocketError::ConnectionReset:
                setError(SocketError::ConnectionReset, "Connection reset by peer");
                return -1;
            default:
                setError(forSend ? SocketError::SendFailed : SocketError::ReceiveFailed, failMsg);
                return -1;
        }
    }
}

int SocketImpl::send(const void* data, size_t length) {
    return doTransfer_(true, nullptr, data, length, "send() timed out", "Failed to send data");
}

int SocketImpl::receive(void* buffer, size_t length) {
    return doTransfer_(false, buffer, nullptr, length, "recv() timed out", "Failed to receive data");
}

int SocketImpl::sendTo(const void* data, size_t length, const Endpoint& remote) {
    if (!isValid()) { setError(SocketError::InvalidSocket, "Socket is not valid"); return -1; }
    sockaddr_storage addr{};
    socklen_t addrLen = 0;
    {
        auto rr = resolveToSockaddr(remote.address, remote.port, remote.family, socketType, /*doDns=*/false, addr, addrLen);
        if (rr != SocketError::None) {
            setError(SocketError::SendFailed, "sendTo(): invalid destination address '" + remote.address + "'");
            return -1;
        }
    }
    for (;;) {
#ifdef _WIN32
        int sent = ::sendto(socketHandle, static_cast<const char*>(data), static_cast<int>(length), 0, reinterpret_cast<sockaddr*>(&addr), addrLen);
#elif defined(MSG_NOSIGNAL)
        ssize_t sent = ::sendto(socketHandle, data, length, MSG_NOSIGNAL, reinterpret_cast<sockaddr*>(&addr), addrLen);
#else
        ssize_t sent = ::sendto(socketHandle, data, length, 0, reinterpret_cast<sockaddr*>(&addr), addrLen);
#endif
        if (sent != SOCKET_ERROR_CODE) { lastError = SocketError::None; return static_cast<int>(sent); }
        int sysErr = getLastSystemError();
#ifndef _WIN32
        if (sysErr == EINTR) continue;
#endif
        switch (classifyTransferSysError(sysErr)) {
            case SocketError::WouldBlock: setError(SocketError::WouldBlock, "Operation would block"); return -1;
            case SocketError::Timeout: setError(SocketError::Timeout, "sendTo() timed out"); return -1;
            case SocketError::ConnectionReset: setError(SocketError::ConnectionReset, "Connection reset by peer"); return -1;
            default: setError(SocketError::SendFailed, "sendTo() failed"); return -1;
        }
    }
}

int SocketImpl::receiveFrom(void* buffer, size_t length, Endpoint& remote) {
    if (!isValid()) { setError(SocketError::InvalidSocket, "Socket is not valid"); return -1; }
    sockaddr_storage addr{};
    socklen_t addrLen = static_cast<socklen_t>(sizeof(addr));
    for (;;) {
#ifdef _WIN32
        int recvd = ::recvfrom(socketHandle, static_cast<char*>(buffer), static_cast<int>(length), 0, reinterpret_cast<sockaddr*>(&addr), &addrLen);
#else
        ssize_t recvd = ::recvfrom(socketHandle, buffer, length, 0, reinterpret_cast<sockaddr*>(&addr), &addrLen);
#endif
        if (recvd != SOCKET_ERROR_CODE) { remote = endpointFromSockaddr(addr); lastError = SocketError::None; return static_cast<int>(recvd); }
        int sysErr = getLastSystemError();
#ifndef _WIN32
        if (sysErr == EINTR) continue;
#endif
        switch (classifyTransferSysError(sysErr)) {
            case SocketError::WouldBlock: setError(SocketError::WouldBlock, "Operation would block"); return -1;
            case SocketError::Timeout: setError(SocketError::Timeout, "recvfrom() timed out"); return -1;
            case SocketError::ConnectionReset: setError(SocketError::ConnectionReset, "Connection reset by peer"); return -1;
            default: setError(SocketError::ReceiveFailed, "receiveFrom() failed"); return -1;
        }
    }
}

bool SocketImpl::sendAll(const void* data, size_t length) {
    const auto* ptr = static_cast<const char*>(data);
    size_t remaining = length;
    while (remaining > 0) {
        int sent = send(ptr, remaining);
        if (sent < 0) return false;
        ptr += static_cast<size_t>(sent);
        remaining -= static_cast<size_t>(sent);
    }
    lastError = SocketError::None;
    return true;
}

bool SocketImpl::receiveAll(void* buffer, size_t length) {
    auto* ptr = static_cast<char*>(buffer);
    size_t remaining = length;
    while (remaining > 0) {
        int got = receive(ptr, remaining);
        if (got < 0) return false;
        if (got == 0) { setError(SocketError::ConnectionReset, "Connection closed before all bytes received"); return false; }
        ptr += static_cast<size_t>(got);
        remaining -= static_cast<size_t>(got);
    }
    lastError = SocketError::None;
    return true;
}

bool SocketImpl::waitReady(bool forRead, std::chrono::milliseconds timeout) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        auto sliceMs = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (sliceMs < 0) return false;
#if defined(__APPLE__) || defined(__FreeBSD__)
        int evFd = ::kqueue();
        if (evFd == -1) return false;
        struct kevent reg{};
        int16_t filter = forRead ? EVFILT_READ : EVFILT_WRITE;
        EV_SET(&reg, static_cast<uintptr_t>(socketHandle), filter, EV_ADD | EV_ENABLE, 0, 0, nullptr);
        if (::kevent(evFd, &reg, 1, nullptr, 0, nullptr) == -1) { ::close(evFd); return false; }
        struct timespec ts{}; ts.tv_sec = sliceMs / 1000; ts.tv_nsec = (sliceMs % 1000) * 1000000;
        struct kevent out{}; int nReady = ::kevent(evFd, nullptr, 0, &out, 1, &ts);
        ::close(evFd);
#elif defined(__linux__)
        int evFd = ::epoll_create1(EPOLL_CLOEXEC);
        if (evFd == -1) return false;
        struct epoll_event epev{}; epev.events = (forRead ? EPOLLIN : EPOLLOUT) | EPOLLERR;
        epev.data.fd = socketHandle;
        if (::epoll_ctl(evFd, EPOLL_CTL_ADD, socketHandle, &epev) == -1) { ::close(evFd); return false; }
        struct epoll_event outev{}; int nReady = ::epoll_wait(evFd, &outev, 1, static_cast<int>(sliceMs));
        ::close(evFd);
#elif defined(_WIN32)
        WSAPOLLFD pfd{}; pfd.fd = socketHandle; pfd.events = forRead ? POLLIN : POLLOUT;
        int nReady = ::WSAPoll(&pfd, 1, static_cast<int>(sliceMs));
#endif
        if (nReady <= 0) return false;
        return true;
    }
}

bool SocketImpl::waitWithTimeoutError_(bool forRead, Milliseconds timeout, const char* timeoutErrorMsg) {
    if (!isValid()) { setError(SocketError::InvalidSocket, "Socket is not valid"); return false; }
    if (!waitReady(forRead, std::chrono::milliseconds(timeout.count))) { setError(SocketError::Timeout, timeoutErrorMsg); return false; }
    return true;
}

bool SocketImpl::waitReadable(Milliseconds timeout) { return waitWithTimeoutError_(true, timeout, "waitReadable timed out"); }
bool SocketImpl::waitWritable(Milliseconds timeout) { return waitWithTimeoutError_(false, timeout, "waitWritable timed out"); }

} // namespace aiSocks
