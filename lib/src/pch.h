// pch.h  Precompiled header for aiSocksLib.
//
// Contains the subset of standard-library and platform headers that are
// included by nearly every translation unit in the library.  Parsed once per
// build type/compiler flags combination and reused by all TUs via
//   target_precompile_headers(aiSocksLib PRIVATE src/pch.h)
//
// Rules:
//    Only headers that are both LARGE and STABLE belong here.
//    Project headers (Socket.h, SocketImpl.h, ) do NOT belong here 
//     they change frequently and would invalidate the PCH on every edit.
//    The #ifdef guards mirror what SocketImpl.h already includes so there
//     is no double-parse penalty; clang/MSVC detect the guard and skip the
//     file body when it was already seen via the PCH.

//  C++ standard library 
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <functional>

//  Platform socket / OS headers 
#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <iphlpapi.h>
#else
#  include <sys/socket.h>
#  include <sys/types.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <netdb.h>
#  include <errno.h>
#  include <ifaddrs.h>
#  include <net/if.h>
#  include <signal.h>
#endif
