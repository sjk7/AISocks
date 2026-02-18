// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
//
// UdpSocket.cpp  does NOT include SocketImpl.h.  All operations are
// dispatched through Socket's protected do*() bridge methods, whose bodies
// live in Socket.cpp  the single pImpl firewall.
#include "UdpSocket.h"

namespace aiSocks {

UdpSocket::UdpSocket(AddressFamily family) : Socket(SocketType::UDP, family) {}

} // namespace aiSocks
