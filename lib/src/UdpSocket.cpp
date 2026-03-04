//
// UdpSocket.cpp  does NOT include SocketImpl.h.  All operations are
// dispatched through Socket's protected do*() bridge methods, whose bodies
// live in Socket.cpp  the single pImpl firewall.
#ifdef _WIN32
#include "pch.h"
#endif
#include "UdpSocket.h"

namespace aiSocks {

UdpSocket::UdpSocket(AddressFamily family) : Socket(SocketType::UDP, family) {}

} // namespace aiSocks
