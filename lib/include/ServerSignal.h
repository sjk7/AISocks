#ifndef AISOCKS_SERVER_SIGNAL_H
#define AISOCKS_SERVER_SIGNAL_H

#include <atomic>

namespace aiSocks {

// ---------------------------------------------------------------------------
// Process-wide SIGINT/SIGTERM flag for ServerBase.
//
// Kept outside the ServerBase<ClientData> template so that there is exactly
// ONE flag regardless of how many different ClientData types are in use.
// A template static member would be instantiated separately for each
// specialisation, meaning only the last signal() registration would ever
// set the flag for its own type — every other instantiation would be silent.
//
// ServerBase instances whose handleSignals_ is true install serverHandleSignal
// as the SIGINT/SIGTERM handler and check g_serverSignalStop each loop
// iteration.  Instances with handleSignals_ == false ignore this flag
// entirely and must be stopped via requestStop().
// ---------------------------------------------------------------------------
extern std::atomic<bool> g_serverSignalStop;

// Plain C signal handler — writes g_serverSignalStop, nothing else.
extern "C" void serverHandleSignal(int) noexcept;

} // namespace aiSocks

#endif // AISOCKS_SERVER_SIGNAL_H
