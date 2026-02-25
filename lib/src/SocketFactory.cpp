// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
#ifdef _WIN32
#include "pch.h"
#endif
#include "SocketFactory.h"
#include "SocketImpl.h"
#include "SocketImplHelpers.h"
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
    std::unique_ptr<SocketImpl> impl, const char* operation,
    AddressFamily family) {

    if (!impl || !impl->isValid()) {
        // Extract error information from the impl if available
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

    // Create the appropriate socket type from the impl
    if constexpr (std::is_same_v<SocketType, TcpSocket>) {
        // For TcpSocket, we need to use the private constructor
        // This requires SocketFactory to be a friend of TcpSocket
        return Result<SocketType>::success(SocketType(std::move(impl)));
    } else if constexpr (std::is_same_v<SocketType, UdpSocket>) {
        // For UdpSocket, use the public constructor
        // Note: We can't inject a custom impl, so we'll create a basic socket
        // and check if it's valid
        try {
            auto socket = SocketType(family);
            if (!socket.isValid()) {
                return Result<SocketType>::failure(socket.getLastError(),
                    operation, SocketFactory::captureLastError(), false);
            }
            return Result<SocketType>::success(std::move(socket));
        } catch (...) {
            return Result<SocketType>::failure(SocketError::CreateFailed,
                operation, SocketFactory::captureLastError(), false);
        }
    } else {
        return Result<SocketType>::failure(
            SocketError::Unknown, "Unsupported socket type", 0, false);
    }
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
            ("bind(" + config.address + ":"
                + std::to_string(config.port.value()) + ")")
                .c_str(),
            SocketFactory::captureLastError(), false);
    }

    return Result<SocketType>::success(std::move(socket));
}

Result<TcpSocket> SocketFactory::listenSocket(TcpSocket&& socket, int backlog) {
    // Check socket validity first
    auto valid_check = checkSocketError(socket, "listen()");
    if (valid_check.isError()) {
        return Result<TcpSocket>::failure(
            valid_check.error(), valid_check.message().c_str(), 0, false);
    }

    // Start listening
    if (!socket.listen(backlog)) {
        return Result<TcpSocket>::failure(socket.getLastError(),
            ("listen(backlog=" + std::to_string(backlog) + ")").c_str(),
            SocketFactory::captureLastError(), false);
    }

    return Result<TcpSocket>::success(std::move(socket));
}

Result<TcpSocket> SocketFactory::connectSocket(
    TcpSocket&& socket, const ConnectArgs& config) {
    // Check socket validity first
    auto valid_check = checkSocketError(socket, "connect()");
    if (valid_check.isError()) {
        return Result<TcpSocket>::failure(
            valid_check.error(), valid_check.message().c_str(), 0, false);
    }

    // Attempt connection
    if (!socket.connect(config.address, config.port, config.connectTimeout)) {
        const char* desc = socket.getLastErrorIsDns() ? "DNS resolution failed"
                                                      : "connect() failed";

        // Get the correct system error code (DNS errors use gai code, not
        // errno)
        int sysCode = socket.getLastErrorSysCode();

        return Result<TcpSocket>::failure(
            socket.getLastError(), desc, sysCode, socket.getLastErrorIsDns());
    }

    return Result<TcpSocket>::success(std::move(socket));
}

// ---------------------------------------------------------------------------
// Public method implementations
// ---------------------------------------------------------------------------

Result<TcpSocket> SocketFactory::createTcpSocketRaw(AddressFamily family) {
    try {
        auto impl = std::make_unique<SocketImpl>(SocketType::TCP, family);
        return createSocketFromImpl<TcpSocket>(std::move(impl), "socket()");
    } catch (...) {
        return Result<TcpSocket>::failure(SocketError::CreateFailed, "socket()",
            SocketFactory::captureLastError(), false);
    }
}

Result<UdpSocket> SocketFactory::createUdpSocketRaw(AddressFamily family) {
    try {
        auto impl = std::make_unique<SocketImpl>(SocketType::UDP, family);
        return createSocketFromImpl<UdpSocket>(
            std::move(impl), "socket()", family);
    } catch (...) {
        return Result<UdpSocket>::failure(SocketError::CreateFailed, "socket()",
            SocketFactory::captureLastError(), false);
    }
}

Result<TcpSocket> SocketFactory::createTcpServer(
    AddressFamily family, const ServerBind& config) {
    // Create basic TCP socket
    auto socket_result = createTcpSocketRaw(family);
    if (socket_result.isError()) {
        return socket_result;
    }

    // Bind the socket
    auto bind_result = bindSocket(std::move(socket_result.value()), config);
    if (bind_result.isError()) {
        return Result<TcpSocket>::failure(
            bind_result.error(), bind_result.message().c_str(), 0, false);
    }

    // Start listening
    return listenSocket(std::move(bind_result.value()), config.backlog);
}

Result<TcpSocket> SocketFactory::createTcpClient(
    AddressFamily family, const ConnectArgs& config) {
    // Create basic TCP socket
    auto socket_result = createTcpSocketRaw(family);
    if (socket_result.isError()) {
        return socket_result;
    }

    // Connect the socket
    return connectSocket(std::move(socket_result.value()), config);
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
    // Try to create a TCP socket and bind to the port
    auto socket_result = createTcpSocketRaw(family);
    if (socket_result.isError()) {
        return Result<bool>::failure(
            socket_result.error(), socket_result.message().c_str(), 0, false);
    }

    // Try to bind
    ServerBind bind_config{
        address, port, 1, false}; // Minimal backlog, no reuse
    auto bind_result
        = bindSocket(std::move(socket_result.value()), bind_config);

    if (bind_result.isSuccess()) {
        // Successfully bound - port was available, but now it's occupied
        // The socket will be closed when it goes out of scope
        return Result<bool>::success(true);
    } else {
        // Failed to bind - check if it's because port is in use
        auto error = bind_result.error();
        if (error == SocketError::BindFailed) {
            return Result<bool>::success(false); // Port is in use
        } else {
            // Some other error occurred
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
    std::unique_ptr<SocketImpl> impl, const char* operation,
    AddressFamily family);

template Result<UdpSocket> SocketFactory::createSocketFromImpl<UdpSocket>(
    std::unique_ptr<SocketImpl> impl, const char* operation,
    AddressFamily family);

template Result<TcpSocket> SocketFactory::bindSocket<TcpSocket>(
    TcpSocket&& socket, const ServerBind& config);

template Result<UdpSocket> SocketFactory::bindSocket<UdpSocket>(
    UdpSocket&& socket, const ServerBind& config);

} // namespace aiSocks
