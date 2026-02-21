// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#ifndef AISOCKS_SOCKET_IMPL_HELPERS_H
#define AISOCKS_SOCKET_IMPL_HELPERS_H

#include "Socket.h"
#include <string>
#include <cstddef>

namespace aiSocks {

// Forward declarations
struct ErrorContext;

// Error classification helper
SocketError classifyTransferSysError(int sysErr) noexcept;

// Address resolution helper
SocketError resolveToSockaddr(const std::string& address, Port port,
    AddressFamily family, SocketType sockType, bool doDns,
    sockaddr_storage& out, socklen_t& outLen, int* gaiErr = nullptr);

// Error message formatting
std::string formatErrorContext(const ErrorContext& ctx);

// Network interface enumeration
std::vector<NetworkInterface> getLocalAddresses();

} // namespace aiSocks

#endif // AISOCKS_SOCKET_IMPL_HELPERS_H
