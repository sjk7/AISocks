// This is an independent project of an individual developer. Dear PVS-Studio, please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com


#include "ServerSignal.h"
#include <atomic>

namespace aiSocks {

// One definition of the process-wide signal flag.
std::atomic<bool> g_serverSignalStop{false};

extern "C" void serverHandleSignal(int) noexcept {
    g_serverSignalStop.store(true, std::memory_order_relaxed);
}

} // namespace aiSocks
