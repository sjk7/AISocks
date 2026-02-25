// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
#ifdef _WIN32
#include "pch.h"
#endif
#include "Socket.h"
#include "SocketImpl.h"
#include "SocketHelpers.h"
#include <cassert>
#include <string>

namespace aiSocks {

namespace {
    // Set error state instead of throwing - returns false on error
    bool setErrorIfFailed(
        bool ok, const std::unique_ptr<SocketImpl>& /*impl*/) {
        if (!ok) {
            // Don't throw - just let the socket remain in invalid state
            // The caller can check isValid() and getLastError()
            return false;
        }
        return true;
    }
} // namespace

// ---------------------------------------------------------------------------
// Endpoint utility methods
// ---------------------------------------------------------------------------

bool Endpoint::isLoopback() const {
    if (family == AddressFamily::IPv4) {
        // Check for 127.x.x.x
        return address.substr(0, 4) == "127.";
    } else {
        // IPv6: check for ::1
        return address == "::1" || address == "::"
            || address == "0000:0000:0000:0000:0000:0000:0000:0001";
    }
}

bool Endpoint::isPrivateNetwork() const {
    if (family == AddressFamily::IPv4) {
        // 10.0.0.0/8
        if (address.substr(0, 3) == "10.") return true;
        // 172.16.0.0/12
        if (address.substr(0, 4) == "172.") {
            int second = std::stoi(address.substr(4, address.find('.', 4) - 4));
            if (second >= 16 && second <= 31) return true;
        }
        // 192.168.0.0/16
        if (address.substr(0, 8) == "192.168.") return true;
        return false;
    } else {
        // IPv6 ULA: fc00::/7 or fd00::/8
        return (address.size() >= 2) && (address[0] == 'f')
            && (address[1] == 'c' || address[1] == 'd');
    }
}

// ---------------------------------------------------------------------------
// Socket class implementation
// ---------------------------------------------------------------------------

Socket::Socket(SocketType type, AddressFamily family)
    : pImpl(std::make_unique<SocketImpl>(type, family)) {
    // Don't throw - let socket remain in invalid state if creation fails
    // Users can check isValid() and getLastError()
}

Socket::Socket(SocketType type, AddressFamily family, const ServerBind& cfg)
    : pImpl(std::make_unique<SocketImpl>(type, family)) {
    // Don't throw - let socket remain in invalid state if creation fails
    if (!setErrorIfFailed(pImpl->isValid(), pImpl)) return;

    if (cfg.reuseAddr) setErrorIfFailed(pImpl->setReuseAddress(true), pImpl);

    setErrorIfFailed(pImpl->bind(cfg.address, cfg.port), pImpl);

    setErrorIfFailed(pImpl->listen(cfg.backlog), pImpl);
}

Socket::Socket(SocketType type, AddressFamily family, const ConnectArgs& cfg)
    : pImpl(std::make_unique<SocketImpl>(type, family)) {
    // Don't throw - let socket remain in invalid state if creation fails
    if (!setErrorIfFailed(pImpl->isValid(), pImpl)) return;

    setErrorIfFailed(
        pImpl->connect(cfg.address, cfg.port, cfg.connectTimeout), pImpl);
}

Socket::Socket(std::unique_ptr<SocketImpl> impl) : pImpl(std::move(impl)) {}

Socket::~Socket() = default;

Socket::Socket(Socket&& other) noexcept : pImpl(std::move(other.pImpl)) {
    // Set moved-from object to an explicitly invalid socket
    other.pImpl = std::make_unique<SocketImpl>();
}

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        pImpl = std::move(other.pImpl);
        // Set moved-from object to an explicitly invalid socket
        other.pImpl = std::make_unique<SocketImpl>();
    }
    return *this;
}

bool Socket::doBind(const std::string& address, Port port) {
    return pImpl->bind(address, port);
}

bool Socket::doListen(int backlog) {
    return pImpl->listen(backlog);
}

std::unique_ptr<SocketImpl> Socket::doAccept() {
    return pImpl->accept();
}

bool Socket::doConnect(
    const std::string& address, Port port, Milliseconds timeout) {
    return pImpl->connect(address, port, timeout);
}

int Socket::doSend(const void* data, size_t length) {
    return pImpl->send(data, length);
}

int Socket::doSend(Span<const std::byte> data) {
    return doSend(data.data(), data.size());
}

int Socket::doReceive(void* buffer, size_t length) {
    return pImpl->receive(buffer, length);
}

int Socket::doReceive(Span<std::byte> buffer) {
    return doReceive(buffer.data(), buffer.size());
}

bool Socket::doSendAll(const void* data, size_t length) {
    return pImpl->sendAll(data, length);
}

bool Socket::doSendAll(Span<const std::byte> data) {
    return doSendAll(data.data(), data.size());
}

bool Socket::doReceiveAll(void* buffer, size_t length) {
    return pImpl->receiveAll(buffer, length);
}

bool Socket::doReceiveAll(Span<std::byte> buffer) {
    return doReceiveAll(buffer.data(), buffer.size());
}

bool Socket::doSendAllProgress(
    const void* data, size_t length, SendProgressSink& progress) {
    const auto* ptr = static_cast<const char*>(data);
    size_t sent = 0;
    while (sent < length) {
        int n = pImpl->send(ptr + sent, length - sent);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
        if (progress(sent, length) < 0) {
            // Caller cancelled  leave lastError as None so the caller
            // can distinguish cancellation from a genuine send error.
            return false;
        }
    }
    return true;
}

int Socket::doSendTo(const void* data, size_t length, const Endpoint& remote) {
    return pImpl->sendTo(data, length, remote);
}

int Socket::doReceiveFrom(void* buffer, size_t length, Endpoint& remote) {
    return pImpl->receiveFrom(buffer, length, remote);
}

int Socket::doSendTo(Span<const std::byte> data, const Endpoint& remote) {
    return doSendTo(data.data(), data.size(), remote);
}

int Socket::doReceiveFrom(Span<std::byte> buffer, Endpoint& remote) {
    return doReceiveFrom(buffer.data(), buffer.size(), remote);
}

Result<void> Socket::setBlocking(bool blocking) {
    if (!pImpl->setBlocking(blocking)) {
        return Result<void>::failure(
            pImpl->getLastError(), "setBlocking", 0, false);
    }
    return Result<void>::success();
}

bool Socket::isBlocking() const noexcept {
    return pImpl->isBlocking();
}

bool Socket::waitReadable(Milliseconds timeout) {
    return pImpl->waitReadable(timeout);
}

bool Socket::waitWritable(Milliseconds timeout) {
    return pImpl->waitWritable(timeout);
}

Result<void> Socket::setReuseAddress(bool reuse) {
    if (!pImpl->setReuseAddress(reuse)) {
        return Result<void>::failure(
            pImpl->getLastError(), "setReuseAddress", 0, false);
    }
    return Result<void>::success();
}

Result<void> Socket::setReusePort(bool enable) {
    if (!pImpl->setReusePort(enable)) {
        return Result<void>::failure(
            pImpl->getLastError(), "setReusePort", 0, false);
    }
    return Result<void>::success();
}

Result<void> Socket::setReceiveTimeout(Milliseconds timeout) {
    if (!pImpl->setReceiveTimeout(timeout)) {
        return Result<void>::failure(
            pImpl->getLastError(), "setReceiveTimeout", 0, false);
    }
    return Result<void>::success();
}

Result<void> Socket::setSendTimeout(Milliseconds timeout) {
    if (!pImpl->setSendTimeout(timeout)) {
        return Result<void>::failure(
            pImpl->getLastError(), "setSendTimeout", 0, false);
    }
    return Result<void>::success();
}

Result<void> Socket::setNoDelay(bool noDelay) {
    if (!pImpl->setNoDelay(noDelay)) {
        return Result<void>::failure(
            pImpl->getLastError(), "setNoDelay", 0, false);
    }
    return Result<void>::success();
}

bool Socket::getNoDelay() const {
    return pImpl->getNoDelay();
}

bool Socket::getLastErrorIsDns() const {
    return pImpl->getLastErrorIsDns();
}

int Socket::getLastErrorSysCode() const {
    return pImpl->getLastErrorSysCode();
}

Result<void> Socket::setKeepAlive(bool enable) {
    if (!pImpl->setKeepAlive(enable)) {
        return Result<void>::failure(
            pImpl->getLastError(), "setKeepAlive", 0, false);
    }
    return Result<void>::success();
}

Result<void> Socket::setLingerAbort(bool enable) {
    if (!pImpl->setLingerAbort(enable)) {
        return Result<void>::failure(
            pImpl->getLastError(), "setLingerAbort", 0, false);
    }
    return Result<void>::success();
}

Result<void> Socket::setBroadcast(bool enable) {
    if (!pImpl->setBroadcast(enable)) {
        return Result<void>::failure(
            pImpl->getLastError(), "setBroadcast", 0, false);
    }
    return Result<void>::success();
}

Result<void> Socket::setMulticastTTL(int ttl) {
    if (!pImpl->setMulticastTTL(ttl)) {
        return Result<void>::failure(
            pImpl->getLastError(), "setMulticastTTL", 0, false);
    }
    return Result<void>::success();
}

Result<void> Socket::setReceiveBufferSize(int bytes) {
    if (!pImpl->setReceiveBufferSize(bytes)) {
        return Result<void>::failure(
            pImpl->getLastError(), "setReceiveBufferSize", 0, false);
    }
    return Result<void>::success();
}

Result<void> Socket::setSendBufferSize(int bytes) {
    if (!pImpl->setSendBufferSize(bytes)) {
        return Result<void>::failure(
            pImpl->getLastError(), "setSendBufferSize", 0, false);
    }
    return Result<void>::success();
}

int Socket::getReceiveBufferSize() const {
    return pImpl->getReceiveBufferSize();
}

int Socket::getSendBufferSize() const {
    return pImpl->getSendBufferSize();
}

bool Socket::shutdown(ShutdownHow how) {
    return pImpl->shutdown(how);
}

void Socket::close() noexcept {
    if (pImpl) {
        pImpl->close();
    }
}

bool Socket::isValid() const noexcept {
    return pImpl && pImpl->isValid();
}

AddressFamily Socket::getAddressFamily() const noexcept {
    return pImpl->getAddressFamily();
}

SocketError Socket::getLastError() const noexcept {
    return pImpl->getLastError();
}

std::string Socket::getErrorMessage() const {
    return pImpl->getErrorMessage();
}

Result<Endpoint> Socket::getLocalEndpoint() const {
    auto endpoint = pImpl->getLocalEndpoint();
    if (endpoint) {
        return Result<Endpoint>::success(endpoint.value());
    } else {
        return Result<Endpoint>::failure(
            pImpl->getLastError(), "getLocalEndpoint", 0, false);
    }
}

Result<Endpoint> Socket::getPeerEndpoint() const {
    auto endpoint = pImpl->getPeerEndpoint();
    if (endpoint) {
        return Result<Endpoint>::success(endpoint.value());
    } else {
        return Result<Endpoint>::failure(
            pImpl->getLastError(), "getPeerEndpoint", 0, false);
    }
}

NativeHandle Socket::getNativeHandle() const noexcept {
    return static_cast<NativeHandle>(pImpl->getRawHandle());
}

// Static utility methods
std::vector<NetworkInterface> Socket::getLocalAddresses() {
    return SocketImpl::getLocalAddresses();
}

bool Socket::isValidIPv4(const std::string& address) {
    return SocketImpl::isValidIPv4(address);
}

bool Socket::isValidIPv6(const std::string& address) {
    return SocketImpl::isValidIPv6(address);
}

std::string Socket::ipToString(const void* addr, AddressFamily family) {
    return SocketImpl::ipToString(addr, family);
}

} // namespace aiSocks
