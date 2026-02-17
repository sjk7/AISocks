// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
#include "Socket.h"
#include "SocketImpl.h"
#include <string>

namespace aiSocks {

// Helper used only inside constructors: if ok is false, extract the error
// from pImpl and throw with the failing step name prepended.
namespace {
    void throwIfFailed(bool ok, const std::string& step,
        const std::unique_ptr<SocketImpl>& impl) {
        if (!ok) {
            SocketError err = impl->getLastError();
            std::string msg = step + ": " + impl->getErrorMessage();
            throw SocketException(err, msg);
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

bool Socket::bind(const std::string& address, Port port) {
    if (!pImpl) return false;
    return pImpl->bind(address, port);
}

bool Socket::listen(int backlog) {
    if (!pImpl) return false;
    return pImpl->listen(backlog);
}

std::unique_ptr<Socket> Socket::accept() {
    if (!pImpl) return nullptr;
    auto clientImpl = pImpl->accept();
    if (!clientImpl) {
        return nullptr;
    }
    return std::unique_ptr<Socket>(new Socket(std::move(clientImpl)));
}

bool Socket::connect(const std::string& address, Port port) {
    if (!pImpl) return false;
    return pImpl->connect(address, port);
}

int Socket::send(const void* data, size_t length) {
    if (!pImpl) return -1;
    return pImpl->send(data, length);
}

int Socket::receive(void* buffer, size_t length) {
    if (!pImpl) return -1;
    return pImpl->receive(buffer, length);
}

int Socket::sendTo(const void* data, size_t length, const Endpoint& remote) {
    if (!pImpl) return -1;
    return pImpl->sendTo(data, length, remote);
}

int Socket::receiveFrom(void* buffer, size_t length, Endpoint& remote) {
    if (!pImpl) return -1;
    return pImpl->receiveFrom(buffer, length, remote);
}

bool Socket::setBlocking(bool blocking) {
    if (!pImpl) return false;
    return pImpl->setBlocking(blocking);
}

bool Socket::isBlocking() const {
    if (!pImpl) return true; // Default to blocking for moved-from state
    return pImpl->isBlocking();
}

bool Socket::setReuseAddress(bool reuse) {
    if (!pImpl) return false;
    return pImpl->setReuseAddress(reuse);
}

bool Socket::setTimeout(Milliseconds timeout) {
    if (!pImpl) return false;
    return pImpl->setTimeout(timeout);
}

bool Socket::setSendTimeout(Milliseconds timeout) {
    if (!pImpl) return false;
    return pImpl->setSendTimeout(timeout);
}

bool Socket::setNoDelay(bool noDelay) {
    if (!pImpl) return false;
    return pImpl->setNoDelay(noDelay);
}

bool Socket::setKeepAlive(bool enable) {
    if (!pImpl) return false;
    return pImpl->setKeepAlive(enable);
}

bool Socket::shutdown(ShutdownHow how) {
    if (!pImpl) return false;
    return pImpl->shutdown(how);
}

void Socket::close() {
    if (pImpl) {
        pImpl->close();
    }
}

bool Socket::isValid() const {
    return pImpl && pImpl->isValid();
}

AddressFamily Socket::getAddressFamily() const {
    if (!pImpl) return AddressFamily::IPv4; // Default for moved-from state
    return pImpl->getAddressFamily();
}

SocketError Socket::getLastError() const {
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

} // namespace aiSocks
