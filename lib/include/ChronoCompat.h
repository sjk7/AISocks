// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
#ifndef AISOCKS_CHRONO_COMPAT_H
#define AISOCKS_CHRONO_COMPAT_H

#include "SocketTypes.h"
#include <chrono>

namespace aiSocks {

// Conversion functions between aiSocks::Milliseconds and std::chrono types
// Include this header in .cpp files that need chrono compatibility.

inline Milliseconds toMilliseconds(std::chrono::milliseconds ms) {
    return Milliseconds{ms.count()};
}

template<typename Rep, typename Period>
inline Milliseconds toMilliseconds(std::chrono::duration<Rep, Period> d) {
    return Milliseconds{std::chrono::duration_cast<std::chrono::milliseconds>(d).count()};
}

inline std::chrono::milliseconds toChronoMilliseconds(Milliseconds ms) {
    return std::chrono::milliseconds{ms.count};
}

} // namespace aiSocks

#endif // AISOCKS_CHRONO_COMPAT_H
