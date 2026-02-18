// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
#ifndef AISOCKS_POLLER_H
#define AISOCKS_POLLER_H

#include "Socket.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace aiSocks {

// Event flags used when registering interest and returned in PollResult.
enum class PollEvent : uint8_t {
    Readable = 1,
    Writable = 2,
    Error = 4,
};

inline PollEvent operator|(PollEvent a, PollEvent b) noexcept {
    return static_cast<PollEvent>(
        static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

inline bool hasFlag(PollEvent set, PollEvent flag) noexcept {
    return (static_cast<uint8_t>(set) & static_cast<uint8_t>(flag)) != 0;
}

// Describes a single ready socket returned by Poller::wait().
// `socket` is a borrowed pointer — the Socket must outlive its Poller
// registration.
struct PollResult {
    const Socket* socket; // borrowed — do not store
    PollEvent events;
};

// Platform-native readiness notification.
//
// Ownership rule: Poller borrows Socket references.  If a Socket is
// destroyed while still registered, call remove() first.
//
// Thread-safety: none — do not share a Poller across threads without
// external synchronisation.
//
// Backend selection (compile-time):
//   macOS / BSD   → kqueue
//   Linux          → epoll
//   Windows        → WSAPoll
class Poller {
    public:
    // Throws SocketException(CreateFailed) if the OS event queue cannot
    // be created (e.g. kqueue() / epoll_create1() failure).
    Poller();
    ~Poller();

    Poller(const Poller&) = delete;
    Poller& operator=(const Poller&) = delete;

    // Register `s` for the given event(s).  Returns false if the socket
    // is invalid; throws SocketException on a hard OS error.
    bool add(const Socket& s, PollEvent interest);

    // Replace the registered interest mask for an already-registered socket.
    bool modify(const Socket& s, PollEvent interest);

    // Deregister a socket.  Safe to call on a socket that is not registered.
    bool remove(const Socket& s);

    // Block until at least one registered socket becomes ready, or until
    // `timeout` elapses.
    //
    //   timeout >= Milliseconds{0} — wait at most that long.
    //   timeout == Milliseconds{-1} — wait forever (until an event arrives).
    //
    // Returns the ready set (may be empty on timeout).
    // Throws SocketException on a hard system error.
    std::vector<PollResult> wait(Milliseconds timeout = Milliseconds{-1});

    struct Impl; // platform-specific; defined in Poller*.cpp

    private:
    std::unique_ptr<Impl> pImpl_;
};

} // namespace aiSocks

#endif // AISOCKS_POLLER_H
