// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#ifndef AISOCKS_UNIX_SOCKET_H
#define AISOCKS_UNIX_SOCKET_H

#ifndef _WIN32

#include "Socket.h"

namespace aiSocks {

// ---------------------------------------------------------------------------
// UnixSocket — stream socket over AF_UNIX (Linux/macOS).
//
// The socket path is stored in Endpoint::address; port is always Port{0}.
// Mirrors TcpSocket API where applicable.
// ---------------------------------------------------------------------------
class UnixSocket : public Socket {
    friend class SocketFactory;

public:
    UnixSocket(UnixSocket&&) noexcept = default;
    UnixSocket& operator=(UnixSocket&&) noexcept = default;

    [[nodiscard]] std::unique_ptr<UnixSocket> accept();

    int send(const void* data, size_t length)  { return doSend(data, length); }
    int receive(void* buffer, size_t length)   { return doReceive(buffer, length); }
    bool sendAll(const void* data, size_t length)   { return doSendAll(data, length); }
    bool receiveAll(void* buffer, size_t length)    { return doReceiveAll(buffer, length); }

private:
    explicit UnixSocket(std::unique_ptr<SocketImpl> impl);
};

} // namespace aiSocks

#endif // !_WIN32
#endif // AISOCKS_UNIX_SOCKET_H
