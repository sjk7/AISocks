#include "ServerSignal.h"
#include <atomic>

namespace aiSocks {

// One definition of the process-wide signal flag.
std::atomic<bool> g_serverSignalStop{false};

extern "C" void serverHandleSignal(int) noexcept {
    g_serverSignalStop.store(true, std::memory_order_relaxed);
}

} // namespace aiSocks
