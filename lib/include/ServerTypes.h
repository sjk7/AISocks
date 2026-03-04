#ifndef AISOCKS_SERVER_TYPES_H
#define AISOCKS_SERVER_TYPES_H

#include <cstddef>

namespace aiSocks {

// ---------------------------------------------------------------------------
// ClientLimit  --  single source of truth for connection-count caps.
//
// Used by both SimpleServer (pollClients / acceptClients) and ServerBase
// (run()).  Kept in its own header so neither class's header needs to
// depend on the other.
// ---------------------------------------------------------------------------
enum class ClientLimit : size_t {
    Unlimited = 0, // Accept unlimited connections
    Default = 1000, // Default limit for production safety
    Low = 100, // Low resource environments
    Medium = 500, // Medium resource environments
    High = 2000, // High performance servers
    Maximum = 10000 // Reasonable maximum for most systems
};

} // namespace aiSocks

#endif // AISOCKS_SERVER_TYPES_H
