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

namespace aiSocks {

TcpSocket::TcpSocket(AddressFamily family) : Socket(SocketType::TCP, family) {}

TcpSocket TcpSocket::createRaw(AddressFamily family) {
    return TcpSocket(family);
}

TcpSocket::TcpSocket(AddressFamily family, const ServerBind& cfg)
    : Socket(SocketType::TCP, family, cfg) {}

TcpSocket::TcpSocket(AddressFamily family, const ConnectTo& cfg)
    : Socket(SocketType::TCP, family, cfg) {}

TcpSocket::TcpSocket(std::unique_ptr<SocketImpl> impl)
    : Socket(std::move(impl)) {}

std::unique_ptr<TcpSocket> TcpSocket::accept() {
    auto clientImpl = doAccept();
    if (!clientImpl) return nullptr;
    return std::unique_ptr<TcpSocket>(new TcpSocket(std::move(clientImpl)));
}

} // namespace aiSocks
