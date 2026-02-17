#include "Socket.h"
#include "SocketImpl.h"

namespace aiSocks {

Socket::Socket(SocketType type, AddressFamily family)
    : pImpl(std::make_unique<SocketImpl>(type, family))
{
}

Socket::Socket(std::unique_ptr<SocketImpl> impl)
    : pImpl(std::move(impl))
{
}

Socket::~Socket() = default;

Socket::Socket(Socket&& other) noexcept 
    : pImpl(std::move(other.pImpl))
{
    // other.pImpl is now nullptr (moved-from state)
}

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        pImpl = std::move(other.pImpl);
        // other.pImpl is now nullptr (moved-from state)
    }
    return *this;
}

bool Socket::bind(const std::string& address, uint16_t port) {
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

bool Socket::connect(const std::string& address, uint16_t port) {
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

bool Socket::setBlocking(bool blocking) {
    if (!pImpl) return false;
    return pImpl->setBlocking(blocking);
}

bool Socket::isBlocking() const {
    if (!pImpl) return true;  // Default to blocking for moved-from state
    return pImpl->isBlocking();
}

bool Socket::setReuseAddress(bool reuse) {
    if (!pImpl) return false;
    return pImpl->setReuseAddress(reuse);
}

bool Socket::setTimeout(int seconds) {
    if (!pImpl) return false;
    return pImpl->setTimeout(seconds);
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
    if (!pImpl) return AddressFamily::IPv4;  // Default for moved-from state
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
