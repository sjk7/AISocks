// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
//
// TcpSocket.cpp  the only file besides Socket.cpp that includes SocketImpl.h.
// Required here so that accept() can move a unique_ptr<SocketImpl> into a new
// TcpSocket via the private Socket(unique_ptr<SocketImpl>) constructor.
// This does NOT create a second firewall  it uses the existing one in Socket.
#include "TcpSocket.h"
#include "SocketImpl.h"
#include <unistd.h>
#include <fcntl.h>

#ifdef __linux__
#include <sys/sendfile.h>
#elif __APPLE__
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#elif _WIN32
#include <winsock2.h>
#include <windows.h>
#include <mswsock.h>
#endif

namespace aiSocks {

TcpSocket::TcpSocket(AddressFamily family) : Socket(SocketType::TCP, family) {}

TcpSocket TcpSocket::createRaw(AddressFamily family) {
    return TcpSocket(family);
}

TcpSocket::TcpSocket(AddressFamily family, const ServerBind& cfg)
    : Socket(SocketType::TCP, family, cfg) {}

TcpSocket::TcpSocket(AddressFamily family, const ConnectArgs& cfg)
    : Socket(SocketType::TCP, family, cfg) {}

TcpSocket::TcpSocket(std::unique_ptr<SocketImpl> impl)
    : Socket(std::move(impl)) {}

std::unique_ptr<TcpSocket> TcpSocket::accept() {
    auto clientImpl = doAccept();
    if (!clientImpl) return nullptr;
    return std::unique_ptr<TcpSocket>(new TcpSocket(std::move(clientImpl)));
}

int TcpSocket::sendfile(int fd, off_t offset, size_t count) {
    uintptr_t handle = getNativeHandle();
    if (handle == 0) return -1;

    int sockfd = static_cast<int>(handle);
    ssize_t sent = 0;

    // Use OS sendfile for zero-copy transfer
#ifdef __linux__
    sent = ::sendfile(sockfd, fd, &offset, count);
#elif __APPLE__
    // macOS sendfile takes different parameters
    off_t len = count;
    sent = ::sendfile(fd, sockfd, offset, &len, nullptr, 0);
    if (sent == 0)
        sent = len; // macOS returns 0 on success, len contains bytes sent
#elif _WIN32
    // Windows TransmitFile for zero-copy transfer
    (void)offset; // Suppress unused parameter warning
    HANDLE hFile = (HANDLE)_get_osfhandle(fd);
    if (hFile == INVALID_HANDLE_VALUE) return -1;

    // TransmitFile parameters are different
    BOOL result = TransmitFile((SOCKET)sockfd, // socket
        hFile, // file handle
        count, // number of bytes to send
        0, // block size (0 = default)
        nullptr, // overlapped structure
        nullptr, // transmit buffers
        0 // flags
    );
    if (result) {
        sent = count; // Success
    } else {
        sent = -1; // Failure
    }
#endif

    return static_cast<int>(sent);
}

} // namespace aiSocks
