// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it. PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#ifdef _WIN32
#include "pch.h"
#endif
#include "SocketFactory.h"
#include "SocketImpl.h"
#include "SocketImplHelpers.h"
#include "UnixSocket.h"
#include <algorithm>

namespace aiSocks {

// ---------------------------------------------------------------------------
// Platform-specific system error retrieval
// ---------------------------------------------------------------------------
int SocketFactory::captureLastError() {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

// ---------------------------------------------------------------------------
// Template method implementations
// ---------------------------------------------------------------------------

template <typename SocketType>
Result<SocketType> SocketFactory::createSocketFromImpl(
    std::unique_ptr<SocketImpl> impl, const char* operation) {

    if (!impl || !impl->isValid()) {
        SocketError err = SocketError::CreateFailed;
        int sysCode = 0;
        const char* desc = operation;

        if (impl) {
            auto ctx = impl->getErrorContext();
            err = impl->getLastError();
            sysCode = ctx.sysCode;
            desc = ctx.description ? ctx.description : operation;
        }

        return Result<SocketType>::failure(err, desc, sysCode, false);
    }

    return Result<SocketType>::success(SocketType(std::move(impl)));
}

Result<void> SocketFactory::checkSocketError(
    const Socket& socket, const char* operation) {
    if (!socket.isValid()) {
        return Result<void>::failure(socket.getLastError(), operation,
            SocketFactory::captureLastError(), false);
    }
    return Result<void>::success();
}

template <typename SocketType>
Result<SocketType> SocketFactory::bindSocket(
    SocketType&& socket, const ServerBind& config) {
    // Check socket validity first
    auto valid_check = checkSocketError(socket, "bind()");
    if (valid_check.isError()) {
        return Result<SocketType>::failure(
            valid_check.error(), valid_check.message().c_str(), 0, false);
    }

    // Set reuse address if requested
    if (config.reuseAddr) {
        if (!socket.setReuseAddress(true)) {
            return Result<SocketType>::failure(socket.getLastError(),
                "setsockopt(SO_REUSEADDR)", SocketFactory::captureLastError(),
                false);
        }
    }

    // Bind the socket
    if (!socket.bind(config.address, config.port)) {

        return Result<SocketType>::failure(socket.getLastError(),
            "bind() failed - address already in use or invalid",
            SocketFactory::captureLastError(), false);
    }

    return Result<SocketType>::success(std::move(socket));
}

// ---------------------------------------------------------------------------
// Public method implementations
// ---------------------------------------------------------------------------

Result<TcpSocket> SocketFactory::createTcpSocketRaw(AddressFamily family) {
    auto impl = std::make_unique<SocketImpl>(SocketType::TCP, family);
    return createSocketFromImpl<TcpSocket>(std::move(impl), "socket()");
}

Result<UdpSocket> SocketFactory::createUdpSocketRaw(AddressFamily family) {
    auto impl = std::make_unique<SocketImpl>(SocketType::UDP, family);
    return createSocketFromImpl<UdpSocket>(std::move(impl), "socket()");
}

Result<TcpSocket> SocketFactory::createTcpServer(
    AddressFamily family, const ServerBind& config) {

    auto impl = std::make_unique<SocketImpl>(SocketType::TCP, family);
    if (!impl->isValid()) {
        auto ctx = impl->getErrorContext();
        return Result<TcpSocket>::failure(impl->getLastError(),
            ctx.description ? ctx.description : "socket()", ctx.sysCode, false);
    }

    TcpSocket socket(std::move(impl));

    if (config.reuseAddr && !socket.setReuseAddress(true))
        return Result<TcpSocket>::failure(socket.getLastError(),
            "setsockopt(SO_REUSEADDR)", captureLastError(), false);

    if (!socket.bind(config.address, config.port))
        return Result<TcpSocket>::failure(socket.getLastError(),
            "bind() failed - address already in use or invalid",
            captureLastError(), false);

    if (!socket.listen(config.backlog))
        return Result<TcpSocket>::failure(socket.getLastError(),
            ("listen(backlog=" + std::to_string(config.backlog) + ")").c_str(),
            captureLastError(), false);

    return Result<TcpSocket>::success(std::move(socket));
}

Result<TcpSocket> SocketFactory::createTcpClient(
    AddressFamily family, const ConnectArgs& config) {
    auto impl = std::make_unique<SocketImpl>(SocketType::TCP, family);
    if (!impl->isValid()) {
        auto ctx = impl->getErrorContext();
        return Result<TcpSocket>::failure(impl->getLastError(),
            ctx.description ? ctx.description : "socket()", ctx.sysCode, false);
    }

    TcpSocket socket(std::move(impl));

    if (!socket.connect(config.address, config.port, config.connectTimeout)) {
        return Result<TcpSocket>::failureOwned(
            socket.getLastError(), socket.getErrorMessage());
    }

    return Result<TcpSocket>::success(std::move(socket));
}

Result<UdpSocket> SocketFactory::createUdpServer(
    AddressFamily family, const ServerBind& config) {
    // Create basic UDP socket
    auto socket_result = createUdpSocketRaw(family);
    if (socket_result.isError()) {
        return socket_result;
    }

    // Bind the socket
    return bindSocket(std::move(socket_result.value()), config);
}

Result<bool> SocketFactory::isPortAvailable(
    AddressFamily family, const std::string& address, Port port) {
    auto socket_result = createTcpSocketRaw(family);
    if (socket_result.isError()) {
        return Result<bool>::failure(
            socket_result.error(), socket_result.message().c_str(), 0, false);
    }

    ServerBind bind_config;
    bind_config.address = address;
    bind_config.port = port;
    bind_config.backlog = Backlog{1};
    bind_config.reuseAddr = false;
    bind_config.logStartupErrors = true;
    bind_config.serverName = "";
    auto bind_result
        = bindSocket(std::move(socket_result.value()), bind_config);

    if (bind_result.isSuccess()) {
        return Result<bool>::success(true);
    } else {
        auto error = bind_result.error();
        if (error == SocketError::BindFailed) {
            return Result<bool>::success(false);
        } else {
            return Result<bool>::failure(
                bind_result.error(), bind_result.message().c_str(), 0, false);
        }
    }
}

Result<Port> SocketFactory::findAvailablePort(AddressFamily family,
    const std::string& address, Port startPort, Port endPort) {
    if (startPort.value() > endPort.value()) {
        return Result<Port>::failure(SocketError::Unknown,
            "findAvailablePort: startPort > endPort", 0, false);
    }

    for (uint16_t port = startPort.value(); port <= endPort.value(); ++port) {
        auto available_result = isPortAvailable(family, address, Port{port});
        if (available_result.isError()) {
            return Result<Port>::failure(available_result.error(),
                available_result.message().c_str(), 0, false);
        }

        if (available_result.value()) {
            return Result<Port>::success(Port{port});
        }
    }

    return Result<Port>::failure(SocketError::Unknown,
        "findAvailablePort: no available ports in range", 0, false);
}

// ---------------------------------------------------------------------------
// Explicit template instantiations
// ---------------------------------------------------------------------------

template Result<TcpSocket> SocketFactory::createSocketFromImpl<TcpSocket>(
    std::unique_ptr<SocketImpl> impl, const char* operation);

template Result<UdpSocket> SocketFactory::createSocketFromImpl<UdpSocket>(
    std::unique_ptr<SocketImpl> impl, const char* operation);

template Result<TcpSocket> SocketFactory::bindSocket<TcpSocket>(
    TcpSocket&& socket, const ServerBind& config);

template Result<UdpSocket> SocketFactory::bindSocket<UdpSocket>(
    UdpSocket&& socket, const ServerBind& config);

Result<TcpSocket> SocketFactory::createTcpSocket(AddressFamily family) {
    return createTcpSocketRaw(family);
}

Result<UdpSocket> SocketFactory::createUdpSocket(AddressFamily family) {
    return createUdpSocketRaw(family);
}

#ifdef AISOCKS_HAVE_UNIX_SOCKETS
Result<UnixSocket> SocketFactory::createUnixServer(const UnixPath& path) {
    auto impl
        = std::make_unique<SocketImpl>(SocketType::TCP, AddressFamily::Unix);
    if (!impl->isValid()) {
        auto ctx = impl->getErrorContext();
        return Result<UnixSocket>::failure(impl->getLastError(),
            ctx.description ? ctx.description : "socket()", ctx.sysCode, false);
    }
    if (!impl->bind(path.value(), Port{0})) {
        auto ctx = impl->getErrorContext();
        return Result<UnixSocket>::failure(impl->getLastError(),
            ctx.description ? ctx.description : "bind() failed", ctx.sysCode,
            false);
    }
    if (!impl->listen(128)) {
        auto ctx = impl->getErrorContext();
        return Result<UnixSocket>::failure(impl->getLastError(),
            ctx.description ? ctx.description : "listen() failed", ctx.sysCode,
            false);
    }
    return Result<UnixSocket>::success(UnixSocket(std::move(impl)));
}

Result<UnixSocket> SocketFactory::createUnixClient(const UnixPath& path) {
    auto impl
        = std::make_unique<SocketImpl>(SocketType::TCP, AddressFamily::Unix);
    if (!impl->isValid()) {
        auto ctx = impl->getErrorContext();
        return Result<UnixSocket>::failure(impl->getLastError(),
            ctx.description ? ctx.description : "socket()", ctx.sysCode, false);
    }
    if (!impl->connect(path.value(), Port{0}, defaultConnectTimeout)) {
        auto ctx = impl->getErrorContext();
        return Result<UnixSocket>::failure(impl->getLastError(),
            ctx.description ? ctx.description : "connect() failed", ctx.sysCode,
            false);
    }
    return Result<UnixSocket>::success(UnixSocket(std::move(impl)));
}

#ifndef _WIN32
std::pair<Result<UnixSocket>, Result<UnixSocket>>
SocketFactory::createUnixPair() {
    int fds[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
        int e = errno;
        return {Result<UnixSocket>::failure(
                    SocketError::CreateFailed, "socketpair() failed", e, false),
            Result<UnixSocket>::failure(
                SocketError::CreateFailed, "socketpair() failed", e, false)};
    }
    auto implA = std::make_unique<SocketImpl>(
        fds[0], SocketType::TCP, AddressFamily::Unix);
    auto implB = std::make_unique<SocketImpl>(
        fds[1], SocketType::TCP, AddressFamily::Unix);
    return {Result<UnixSocket>::success(UnixSocket(std::move(implA))),
        Result<UnixSocket>::success(UnixSocket(std::move(implB)))};
}
#endif // !_WIN32
#endif // AISOCKS_HAVE_UNIX_SOCKETS

} // namespace aiSocks
