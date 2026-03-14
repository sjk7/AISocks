// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it. PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#include "ServerSignal.h"
#include <atomic>
#include <csignal>
#include <mutex>

namespace aiSocks {

// One definition of the process-wide signal flag.
std::atomic<bool> g_serverSignalStop{false};

extern "C" void serverHandleSignal(int) noexcept {
    g_serverSignalStop.store(true, std::memory_order_relaxed);
}

void installSignalHandlers() {
    static std::once_flag installed;
    std::call_once(installed, []() {
        // Install signal handlers for graceful shutdown.
        // This works on Unix-like systems and Windows.
        std::signal(SIGINT, serverHandleSignal);
        std::signal(SIGTERM, serverHandleSignal);

#ifdef SIGBREAK
        // Windows-specific signal for Ctrl+Break
        std::signal(SIGBREAK, serverHandleSignal);
#endif
    });
}

} // namespace aiSocks
