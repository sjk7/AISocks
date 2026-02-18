// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
#include "Socket.h"
#include "SocketImpl.h"
#include <cassert>
#include <string>

namespace aiSocks {

// Helper used only inside constructors: if ok is false, extract the error
// from pImpl and throw with the failing step name prepended.
// Lazy what() for SocketException — built once on first call.
const char* SocketException::what() const noexcept {
    if (!whatCache_.empty()) return whatCache_.c_str();
    try {
        whatCache_ = step_ + ": "
            + formatErrorContext({description_.c_str(), sysCode_, isDns_});
    } catch (...) {
        return "SocketException (error formatting failed)";
    }
    return whatCache_.c_str();
}

namespace {
    void throwIfFailed(bool ok, const std::string& step,
        const std::unique_ptr<SocketImpl>& impl) {
        if (!ok) {
            auto ctx = impl->getErrorContext();
            throw SocketException(impl->getLastError(), step, ctx.description,
                ctx.sysCode, ctx.isDns);
        }
    }
} // namespace

Socket::Socket(SocketType type, AddressFamily family)
    : pImpl(std::make_unique<SocketImpl>(type, family)) {
    throwIfFailed(pImpl->isValid(), "socket()", pImpl);
}

Socket::Socket(SocketType type, AddressFamily family, const ServerBind& cfg)
    : pImpl(std::make_unique<SocketImpl>(type, family)) {
    throwIfFailed(pImpl->isValid(), "socket()", pImpl);

    if (cfg.reuseAddr)
        throwIfFailed(
            pImpl->setReuseAddress(true), "setsockopt(SO_REUSEADDR)", pImpl);

    throwIfFailed(pImpl->bind(cfg.address, cfg.port),
        "bind(" + cfg.address + ":" + std::to_string(cfg.port) + ")", pImpl);

    throwIfFailed(pImpl->listen(cfg.backlog),
        "listen(backlog=" + std::to_string(cfg.backlog) + ")", pImpl);
}

Socket::Socket(SocketType type, AddressFamily family, const ConnectTo& cfg)
    : pImpl(std::make_unique<SocketImpl>(type, family)) {
    throwIfFailed(pImpl->isValid(), "socket()", pImpl);

    throwIfFailed(pImpl->connect(cfg.address, cfg.port, cfg.connectTimeout),
        "connect(" + cfg.address + ":" + std::to_string(cfg.port) + ")", pImpl);
}

Socket::Socket(std::unique_ptr<SocketImpl> impl) : pImpl(std::move(impl)) {}

Socket::~Socket() = default;

Socket::Socket(Socket&& other) noexcept : pImpl(std::move(other.pImpl)) {
    // other.pImpl is now nullptr (moved-from state)
}

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        pImpl = std::move(other.pImpl);
        // other.pImpl is now nullptr (moved-from state)
    }
    return *this;
}

bool Socket::doBind(const std::string& address, Port port) {
    assert(pImpl);
    return pImpl->bind(address, port);
}

bool Socket::doListen(int backlog) {
    assert(pImpl);
    return pImpl->listen(backlog);
}

std::unique_ptr<SocketImpl> Socket::doAccept() {
    assert(pImpl);
    return pImpl->accept();
}

bool Socket::doConnect(
    const std::string& address, Port port, Milliseconds timeout) {
    assert(pImpl);
    return pImpl->connect(address, port, timeout);
}

int Socket::doSend(const void* data, size_t length) {
    assert(pImpl);
    return pImpl->send(data, length);
}

int Socket::doReceive(void* buffer, size_t length) {
    assert(pImpl);
    return pImpl->receive(buffer, length);
}

bool Socket::doSendAll(const void* data, size_t length) {
    assert(pImpl);
    return pImpl->sendAll(data, length);
}

bool Socket::doSendAll(Span<const std::byte> data) {
    return doSendAll(data.data(), data.size());
}

bool Socket::doReceiveAll(void* buffer, size_t length) {
    assert(pImpl);
    return pImpl->receiveAll(buffer, length);
}

bool Socket::doReceiveAll(Span<std::byte> buffer) {
    return doReceiveAll(buffer.data(), buffer.size());
}

// Span overloads — delegate to the raw-pointer implementations.
int Socket::doSend(Span<const std::byte> data) {
    return doSend(data.data(), data.size());
}

int Socket::doReceive(Span<std::byte> buffer) {
    return doReceive(buffer.data(), buffer.size());
}

int Socket::doSendTo(const void* data, size_t length, const Endpoint& remote) {
    assert(pImpl);
    return pImpl->sendTo(data, length, remote);
}

int Socket::doReceiveFrom(void* buffer, size_t length, Endpoint& remote) {
    assert(pImpl);
    return pImpl->receiveFrom(buffer, length, remote);
}

// Span overloads for UDP sendTo / receiveFrom.
int Socket::doSendTo(Span<const std::byte> data, const Endpoint& remote) {
    return doSendTo(data.data(), data.size(), remote);
}

int Socket::doReceiveFrom(Span<std::byte> buffer, Endpoint& remote) {
    return doReceiveFrom(buffer.data(), buffer.size(), remote);
}

bool Socket::setBlocking(bool blocking) {
    assert(pImpl);
    return pImpl->setBlocking(blocking);
}

bool Socket::isBlocking() const noexcept {
    assert(pImpl);
    return pImpl->isBlocking();
}

bool Socket::waitReadable(Milliseconds timeout) {
    assert(pImpl);
    return pImpl->waitReadable(timeout);
}

bool Socket::waitWritable(Milliseconds timeout) {
    assert(pImpl);
    return pImpl->waitWritable(timeout);
}

bool Socket::setReuseAddress(bool reuse) {
    assert(pImpl);
    return pImpl->setReuseAddress(reuse);
}

bool Socket::setReusePort(bool enable) {
    assert(pImpl);
    return pImpl->setReusePort(enable);
}

bool Socket::setTimeout(Milliseconds timeout) {
    assert(pImpl);
    return pImpl->setTimeout(timeout);
}

bool Socket::setSendTimeout(Milliseconds timeout) {
    assert(pImpl);
    return pImpl->setSendTimeout(timeout);
}

bool Socket::setNoDelay(bool noDelay) {
    assert(pImpl);
    return pImpl->setNoDelay(noDelay);
}

bool Socket::setReceiveBufferSize(int bytes) {
    assert(pImpl);
    return pImpl->setReceiveBufferSize(bytes);
}

bool Socket::setSendBufferSize(int bytes) {
    assert(pImpl);
    return pImpl->setSendBufferSize(bytes);
}

bool Socket::setKeepAlive(bool enable) {
    assert(pImpl);
    return pImpl->setKeepAlive(enable);
}

bool Socket::setLingerAbort(bool enable) {
    assert(pImpl);
    return pImpl->setLingerAbort(enable);
}

bool Socket::doSetBroadcast(bool enable) {
    assert(pImpl);
    return pImpl->setBroadcast(enable);
}

bool Socket::shutdown(ShutdownHow how) {
    assert(pImpl);
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
    if (!pImpl) return AddressFamily::IPv4; // Default for moved-from state
    return pImpl->getAddressFamily();
}

SocketError Socket::getLastError() const noexcept {
    if (!pImpl) return SocketError::InvalidSocket;
    return pImpl->getLastError();
}

std::string Socket::getErrorMessage() const {
    if (!pImpl) return "Invalid socket (moved-from state)";
    return pImpl->getErrorMessage();
}

std::optional<Endpoint> Socket::getLocalEndpoint() const {
    if (!pImpl) return std::nullopt;
    return pImpl->getLocalEndpoint();
}

std::optional<Endpoint> Socket::getPeerEndpoint() const {
    if (!pImpl) return std::nullopt;
    return pImpl->getPeerEndpoint();
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

uintptr_t Socket::getNativeHandle() const noexcept {
    if (!pImpl) return static_cast<uintptr_t>(-1);
    return static_cast<uintptr_t>(pImpl->getRawHandle());
}

} // namespace aiSocks
