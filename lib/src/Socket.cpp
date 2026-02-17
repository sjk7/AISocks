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

Socket::Socket(Socket&& other) noexcept = default;

Socket& Socket::operator=(Socket&& other) noexcept = default;

bool Socket::bind(const std::string& address, uint16_t port) {
    return pImpl->bind(address, port);
}

bool Socket::listen(int backlog) {
    return pImpl->listen(backlog);
}

std::unique_ptr<Socket> Socket::accept() {
    auto clientImpl = pImpl->accept();
    if (!clientImpl) {
        return nullptr;
    }
    return std::unique_ptr<Socket>(new Socket(std::move(clientImpl)));
}

bool Socket::connect(const std::string& address, uint16_t port) {
    return pImpl->connect(address, port);
}

int Socket::send(const void* data, size_t length) {
    return pImpl->send(data, length);
}

int Socket::receive(void* buffer, size_t length) {
    return pImpl->receive(buffer, length);
}

bool Socket::setBlocking(bool blocking) {
    return pImpl->setBlocking(blocking);
}

bool Socket::isBlocking() const {
    return pImpl->isBlocking();
}

bool Socket::setReuseAddress(bool reuse) {
    return pImpl->setReuseAddress(reuse);
}

bool Socket::setTimeout(int seconds) {
    return pImpl->setTimeout(seconds);
}

void Socket::close() {
    pImpl->close();
}

bool Socket::isValid() const {
    return pImpl->isValid();
}

AddressFamily Socket::getAddressFamily() const {
    return pImpl->getAddressFamily();
}

SocketError Socket::getLastError() const {
    return pImpl->getLastError();
}

std::string Socket::getErrorMessage() const {
    return pImpl->getErrorMessage();
}

} // namespace aiSocks
