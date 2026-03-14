// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#ifndef AISOCKS_CONFIG_H
#define AISOCKS_CONFIG_H

// ---------------------------------------------------------------------------
// AISOCKS_HAVE_UNIX_SOCKETS
//
// Defined when AF_UNIX / sockaddr_un is available on the target platform:
//   - Linux / macOS / other POSIX: always available.
//   - Windows: requires SDK 10.0.17134 (Redstone 4 / RS4) or later.
//     Detected via NTDDI_WIN10_RS4 from <sdkddkver.h>, which must be
//     included before any other Windows headers.
// ---------------------------------------------------------------------------
#ifdef _WIN32
#include <sdkddkver.h>
#if defined(NTDDI_WIN10_RS4) && (NTDDI_VERSION >= NTDDI_WIN10_RS4)
#if defined(__has_include)
#if __has_include(<afunix.h>)
#include <afunix.h>
#if defined(AF_UNIX)
#define AISOCKS_HAVE_UNIX_SOCKETS 1
#endif
#endif
#endif
#endif
#else
#define AISOCKS_HAVE_UNIX_SOCKETS 1
#endif

#endif // AISOCKS_CONFIG_H
