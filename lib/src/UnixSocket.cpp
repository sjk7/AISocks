// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#ifndef _WIN32
#include "UnixSocket.h"
#include "SocketImpl.h"

namespace aiSocks {

UnixSocket::UnixSocket(std::unique_ptr<SocketImpl> impl)
    : Socket(std::move(impl)) {}

std::unique_ptr<UnixSocket> UnixSocket::accept() {
    auto childImpl = doAccept();
    if (!childImpl) return nullptr;
    return std::unique_ptr<UnixSocket>(new UnixSocket(std::move(childImpl)));
}

} // namespace aiSocks
#endif
