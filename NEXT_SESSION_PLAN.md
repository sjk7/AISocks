# AISocks — Next Session Plan

**Last updated:** 2026-02-18  
**Repo:** https://github.com/sjk7/AISocks  
**Branch:** `main`  
**Last commit:** `a33db3c` — "build: remove precompiled header"  
**Build dir (macOS):** `build-mac/`  
**Build command:** `ninja -C build-mac`  
**Test command:** `cd build-mac && ctest --output-on-failure`  
**Test suite:** 11 tests, ~1.7 s wall time  
**Standard:** C++17, clang++ `-Wall -Wextra -Wpedantic -Werror -arch arm64`

---

## What is complete

All of these are committed and pushed.

- All PVS-Studio warnings fixed (V641, V1071, V512, V1086, V1007, V807, V830,
  V565, V103, V106)
- `receiveAll()` added (exact-read counterpart to `sendAll()`), with two tests
- `noexcept` on all non-throwing accessors (`isValid`, `getLastError`,
  `getAddressFamily`, `isBlocking`, `close`) and `Poller` operators
- `setLingerAbort(bool)`, `setReusePort(bool)`, `waitReadable()`,
  `waitWritable()`, `setSendBufferSize()`, `setReceiveBufferSize()`,
  `setKeepAlive()`, `setSendTimeout()` — all implemented
- `Poller` class: kqueue (macOS), epoll (Linux), WSAPoll (Windows), plus
  async-connect support and `test_poller.cpp`
- `.clang-format` workspace config; all source files normalised
- Test suite wall time: 2.4 s → 0.7 s (3×) via reduced sleeps/timeouts
- **TCP/UDP type-safe split** — `TcpSocket` and `UdpSocket` inherit `Socket` via
  pImpl firewall; all protocol methods behind `do*()` bridges; full test suite
  updated; 11/11 tests passing
- `BlockingGuard` RAII — saves/restores OS `O_NONBLOCK` flag around connect;
  no more manual set/restore in call sites; does NOT call `setBlocking()` (which
  would clobber `lastError`)
- `TcpSocket` bare constructor made `private`; `static TcpSocket createRaw(AddressFamily)`
  factory added; all test/example files migrated via script
- `sendAll` progress callback moved behind pImpl firewall — `SendProgressSink`
  abstract base in `Socket` protected section; stack-local `Adapter` template in
  `TcpSocket.h`; `<functional>` removed from public headers; zero heap allocation
- Progress callback returns `int`; `< 0` cancels send loop; `lastError` left as
  `None` on cancel so caller can distinguish cancel from genuine send error
- `setTimeout` renamed to `setReceiveTimeout` for clarity
- Precompiled header removed from build
- `[[nodiscard]]` added to `TcpSocket::connect()`, `TcpSocket::accept()`, and
  `Socket::setBlocking()`
- Redundant `= delete` copy declarations removed from `TcpSocket` and `UdpSocket`
  (already deleted by `Socket` base)
- `ConnectTo::async` fixed and documented: constructor now calls
  `setBlocking(false)` before connect so `BlockingGuard` saves the non-blocking
  state and restores it on exit (socket stays non-blocking for Poller loop);
  passes `Milliseconds{0}` to trigger the immediate-return WouldBlock path;
  `WouldBlock` is not thrown — it is the expected in-progress result

---

## Open items (not yet implemented)

*(none at this time — all known items have been resolved)*

---

## Repository housekeeping notes

- `.clang-format` is in workspace root; all future saves will reformat
  automatically if the editor respects it.
- `build-mac/` is the active macOS build dir. `build/` is a Windows MSVC
  artefact dir — ignore it on macOS.
- `compile_commands.json` is symlinked at root from `build-mac/` for
  clangd/IntelliSense.

