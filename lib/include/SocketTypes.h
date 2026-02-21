// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
#ifndef AISOCKS_SOCKET_TYPES_H
#define AISOCKS_SOCKET_TYPES_H

#include <cstdint>
#include <chrono>

namespace aiSocks {

// Lightweight milliseconds type to avoid including <chrono> in Socket.h.
// Provides implicit conversion from integers and std::chrono::duration types.
struct Milliseconds {
    int64_t count;

    constexpr Milliseconds() noexcept : count(0) {}
    constexpr explicit Milliseconds(int64_t ms) noexcept : count(ms) {}

    // Implicit conversion from std::chrono::duration types
    template<typename Rep, typename Period>
    constexpr Milliseconds(std::chrono::duration<Rep, Period> d) noexcept
        : count(std::chrono::duration_cast<std::chrono::milliseconds>(d).count()) {}

    constexpr int64_t milliseconds() const noexcept { return count; }

    constexpr bool operator==(Milliseconds other) const noexcept {
        return count == other.count;
    }
    constexpr bool operator!=(Milliseconds other) const noexcept {
        return count != other.count;
    }
    constexpr bool operator<(Milliseconds other) const noexcept {
        return count < other.count;
    }
    constexpr bool operator<=(Milliseconds other) const noexcept {
        return count <= other.count;
    }
    constexpr bool operator>(Milliseconds other) const noexcept {
        return count > other.count;
    }
    constexpr bool operator>=(Milliseconds other) const noexcept {
        return count >= other.count;
    }
};

// Default timeout applied to all optional timeout parameters.
inline constexpr Milliseconds defaultTimeout{30000}; // 30 seconds
inline constexpr Milliseconds defaultConnectTimeout{10000}; // 10 seconds

// Named timeout constants for common use cases.
namespace Timeouts {
    inline constexpr Milliseconds Immediate{0};
    inline constexpr Milliseconds Short{1000};
    inline constexpr Milliseconds Medium{5000};
    inline constexpr Milliseconds Long{30000};
}

enum class AddressFamily { IPv4, IPv6 };

enum class SocketType { TCP, UDP };

enum class SocketError {
    None,
    CreateFailed,
    BindFailed,
    ListenFailed,
    AcceptFailed,
    ConnectFailed,
    SendFailed,
    ReceiveFailed,
    ConnectionReset, // peer closed / ECONNRESET / EPIPE / WSAECONNRESET
    SetOptionFailed,
    InvalidSocket,
    Timeout,
    WouldBlock,
    Unknown
};

// Controls which direction shutdown() closes.
enum class ShutdownHow {
    Read, // discard queued input; peer SEND will get RST      (SHUT_RD)
    Write, // send FIN; peer recv will see EOF                  (SHUT_WR)
    Both, // both directions                                   (SHUT_RDWR)
};

} // namespace aiSocks

#endif // AISOCKS_SOCKET_TYPES_H
