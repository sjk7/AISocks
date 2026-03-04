// Precompiled header for aiSocks test binaries.
// Covers the headers that appear in the majority of test translation units,
// as identified by -ftime-trace analysis.  Keep this list stable — adding
// volatile or rarely-used headers defeats the purpose.
#pragma once

// ── aiSocks public API (present in 7–21 of 25 test TUs) ─────────────────────
#include "TcpSocket.h" // 21 / 25 tests
#include "SocketFactory.h" // 14 / 25 tests
#include "UdpSocket.h" // 6  / 25 tests
#include "ServerBase.h" // 7  / 25 tests

// ── Standard library (present in 11–19 of 25 test TUs) ──────────────────────
#include <thread> // 19 / 25 tests
#include <chrono> // 18 / 25 tests
#include <string> // 17 / 25 tests
#include <cstring> // 12 / 25 tests
#include <atomic> // 11 / 25 tests
#include <iostream> //  7 / 25 tests
#include <vector> //  5 / 25 tests
#include <cassert> //  2 / 25 tests
