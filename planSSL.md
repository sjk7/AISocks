

## Highest-Priority Findings

### 1. Accept-filter lifecycle bug in `ServerBase`

Severity: High

Problem discovered:

- In the currently reviewed library version, accepted sockets were being added to the poller before `onAcceptFilter()` ran.
- If the filter rejected the connection, the function continued without deregistering the socket from the poller.
- That leaves a stale borrowed pointer registered in the poller until fd reuse makes it dangerous.

Why it matters:

- This can produce wrong-socket event delivery.
- Under fd reuse, it can become a use-after-close style bug.
- This is a correctness bug in connection lifecycle management, not just cleanup hygiene.

Expected fix shape:

- Run `onAcceptFilter()` before poller registration.
- If registration must happen first for some reason, explicitly remove on rejection before the socket is destroyed.
- Add a regression test that forces repeated reject/reuse behavior.

### 1. `HttpFileServer` is not hot-path efficient for large responses

Severity: Medium

Problem discovered:

- `HttpFileServer` reads the entire file into memory.
- It may copy file content into the cache.
- It then copies the bytes again into a single HTTP response buffer.

Why it matters:

- This is the largest mismatch between the library's performance claims and actual end-to-end file-serving behavior.
- Large-file serving pays multiple memory and copy costs.
- The core event loop is efficient, but the higher-level file-serving path undermines it.

Expected fix shape:

- For large files, prefer streaming or `sendfile` where available. Note that in my previous tests, sendfile was actually slower on some platforms. So be careful.

- Consider a split response model: header buffer plus file payload, instead of one monolithic string.
- Keep the current small-file fast path if it is simpler and good enough for cacheable small assets.

### 4. `HttpResponseParser` is too permissive about HTTP version tokens

Severity: Low

Problem discovered:

- Response status-line parsing validates spacing and status code but does not verify the version token is actually HTTP-like.
- A malformed line with spaces and a numeric code may still be accepted.

Why it matters:

- This is surprising parser behavior.
- It weakens strictness and may hide upstream protocol errors.

Expected fix shape:

- Validate that the version token is `HTTP/1.0` or `HTTP/1.1`, or explicitly define the intended leniency.
- Add parser tests for malformed but superficially structured status lines.

## Secondary Observations

- `HttpPollServer` directly prints status/debug output. This is simple, but it reduces library composability for users who want silent embedding or structured logging. Only print to stderr when there is actually a hard error.

- `HttpPollServer` only supports `GET` and `HEAD` in the request-dispatch path. That may be acceptable if intentional, but the restriction is strong enough to deserve extremely explicit API and README wording.
- The core server/poller architecture is strong:
  - sparse fd table plus dense active fd list
  - throttled lazy timeout heap
  - reusable event buffers in poller backends
  - incremental request scanning
  - zero-copy static response path in `HttpPollServer`

## Coverage Assessment

### Missing or weak regression coverage

- Accept-filter rejection path in `ServerBase`
- fd reuse after rejection
- invalid response version/status-line edge cases in `HttpResponseParser`
- large-file performance and allocation regression checks in `HttpFileServer`

### Existing strengths

- `HttpPollServer` coverage is broad.
- `HttpRequest` and `HttpResponseParser` already have dedicated tests.
- There are security- and stress-oriented tests.
- There are multiple server-base and timeout edge-case tests.
- Fuzz targets exist for parsers and path handling.

## Recommended Next Session Order

### Phase 1: Re-verify workspace state before changing code

1. Inspect the current uncommitted changes listed below.
2. Confirm whether the accept-filter bug is already fixed in the working tree.
3. Confirm whether the new IP filtering / access logging work is intentional and desired for this branch.
4. Avoid reverting unrelated user changes.

### Phase 2: Lock down correctness first

1. Add or verify a regression test for accept-filter rejection before any further refactor.
2. Decide and implement explicit `HttpClient` scheme policy for HTTPS.
3. Add parser strictness tests for malformed response status lines.

### Phase 3: Improve file-serving hot path

1. Measure current `HttpFileServer` large-file behavior.
2. Decide whether to use:
   - header + file streaming, or
   - `sendfile`, or
   - a hybrid threshold-based strategy.
3. Add focused performance regression coverage or at least benchmark scripts with clear baselines.

## Concrete Test Additions To Write

### `ServerBase` / accept-filter tests

- Reject a newly accepted connection and confirm it is never serviced.
- Repeat reject/accept cycles enough times to encourage fd reuse.
- Verify no stale events are delivered to a later unrelated connection.


### `HttpResponseParser` tests

- Reject `XYZ 200 OK\r\n\r\n`
- Reject malformed version tokens with valid numeric status
- Confirm intended behavior for HTTP/1.0 vs HTTP/1.1 only

### `HttpFileServer` tests or benchmarks

- Large file response without cache
- Large file response with cache
- Repeated requests to the same asset
- Compare memory/copy behavior before and after any streaming refactor

## Implementation Spec

This section is the execution-oriented version of the handoff. Use it when the next session should write tests and code with minimal re-planning.

### Issue A: `ServerBase` accept-filter lifecycle safety

Priority: P0

Intent:

- Ensure rejected accepted sockets never remain registered in the poller.
- Prove the fix with regression coverage, including probable fd reuse.

Likely files to inspect or change:

- `lib/include/ServerBase.h`
- possibly `lib/include/PollEventLoop.h`
- possibly poller backends only if investigation shows a secondary stale-registration problem
- tests:
  - `tests/test_server_base_edge_cases.cpp`
  - `tests/test_server_base.cpp`
  - `tests/test_fixes.cpp`
  - create a dedicated new test file only if existing server-base tests become too noisy

Implementation notes:

- Preferred behavior is to run `onAcceptFilter()` before poller registration.
- If the current worktree already does that, verify that no later path re-registers or keeps stale state.
- Avoid introducing extra allocations or bookkeeping in the accept hot path unless needed for correctness.

Test design:

- Add a small server type whose `onAcceptFilter()` rejects all loopback clients, or rejects on a toggle/counter.
- Attempt repeated connect/disconnect cycles from a test client.
- Confirm rejected connections never reach `onReadable()`.
- Confirm later accepted connections still behave normally.
- Stress enough cycles to encourage fd reuse.

Acceptance criteria:

- Rejected accepted sockets are not serviced.
- No stale event delivery occurs after repeated rejection cycles.
- Existing server-base tests still pass.

Non-goals:

- Do not redesign the poller API.
- Do not add general-purpose connection filtering features beyond what is needed to close the correctness gap.

### Issue B: `HttpClient` explicit scheme handling

Priority: P1

Intent:

- Remove ambiguous behavior for `https://` and unsupported schemes.
- Make caller-visible behavior explicit and testable.

Likely files to inspect or change:

- `lib/include/HttpClient.h`
- tests:
  - `tests/test_http_client_server.cpp`

Recommended policy:

- Keep the library plain HTTP-only.
- Reject `https://` clearly rather than silently defaulting to port 80.
- Reject any other unsupported scheme clearly as well.

Recommended error shape:

- Return a failure with a stable, human-readable message such as:
  - `HTTPS is not supported`
  - or `Unsupported URL scheme: https`
- Prefer one canonical message so tests do not need fuzzy matching.

Test design:

- Direct request to `https://example.com/` returns immediate error.
- Redirect chain from an HTTP server to an HTTPS location returns the same scheme error.
- Existing `resolveUrl()` tests remain unchanged unless policy requires new expectations.

Acceptance criteria:

- No path silently treats an HTTPS URL as plain HTTP on port 80.
- Direct HTTPS and redirect-to-HTTPS behavior are deterministic and tested.

Non-goals:

- Do not add TLS.
- Do not pull in external crypto or HTTP libraries.

### Issue C: `HttpResponseParser` status-line strictness

Priority: P2

Intent:

- Make response parsing less surprising by rejecting obviously invalid version tokens.

Likely files to inspect or change:

- `lib/src/HttpResponse.cpp`
- tests:
  - `tests/test_http_response_parser.cpp`

Recommended parser policy:

- Accept only `HTTP/1.0` and `HTTP/1.1`.
- Keep the existing tolerance for reason phrase omission if desired.
- Reject malformed version tokens even if spacing and numeric status code are valid.

Test cases to add:

- `XYZ 200 OK\r\n\r\n` is rejected.
- `HTTP/2 200 OK\r\n\r\n` is rejected if the parser is intentionally HTTP/1.x only.
- `HTTP/1.2 200 OK\r\n\r\n` is rejected.
- Existing valid `HTTP/1.0` and `HTTP/1.1` cases continue to pass.

Acceptance criteria:

- Invalid response version/status-line combinations move parser state to `Error`.
- Existing valid response parser tests continue to pass.

Non-goals:

- Do not broaden parser scope to HTTP/2.
- Do not rewrite the parser architecture.

### Issue D: `HttpFileServer` large-file hot path

Priority: P3

Intent:

- Reduce copy and allocation cost for larger file responses without destabilizing the existing HTTP flow.

Likely files to inspect or change:

- `lib/include/HttpFileServer.h`
- `lib/src/HttpFileServer.cpp`
- possibly `lib/include/HttpPollServer.h`
- possibly `lib/src/HttpPollServer.cpp`
- possibly `lib/include/TcpSocket.h` or lower layers if a `sendfile`-style path is used
- tests or benchmarks:
  - `tests/test_advanced_file_server.cpp`
  - `tests/test_file_cache.cpp`
  - a new focused benchmark or stress test if existing tests are not suitable

Recommended design exploration order:

1. Keep current small-file path untouched initially.
2. Add a size threshold for a large-file path.
3. Evaluate one of:
  - header buffer plus streamed file body
  - OS `sendfile` where already supported
  - hybrid by platform and file size

Constraints:

- Preserve current behavior for keep-alive and HEAD responses.
- Preserve existing cache semantics unless deliberately changed.
- Avoid turning `HttpFileServer` into a large state-machine rewrite in the same session as the correctness fixes above.

Suggested benchmark questions:

- How many copies are paid per uncached large-file request?
- How much peak memory is consumed for a single large response?
- Does cached small-file performance regress after introducing a streaming path?

Acceptance criteria:

- A concrete chosen design is written down.
- Either code is implemented with focused tests, or the benchmark data is captured and the chosen design is justified for a later patch.

Non-goals:

- Do not try to solve every file-server feature at once.
- Do not mix this work with unrelated API cleanup.

## Test Placement Guidance

Prefer extending existing tests rather than creating new files unless the scenario is too specialized.

- `ServerBase` lifecycle regression:
  - first choice: `tests/test_server_base_edge_cases.cpp`
  - second choice: `tests/test_fixes.cpp`
- `HttpClient` scheme handling:
  - `tests/test_http_client_server.cpp`
- `HttpResponseParser` strictness:
  - `tests/test_http_response_parser.cpp`
- `HttpFileServer` performance-oriented functional tests:
  - existing file-server tests if behavior assertions are enough
  - separate benchmark/stress target if timing or allocation evidence is needed

## Suggested Execution Order For The Next Coding Session

1. Review dirty worktree changes and decide whether the accept-filter fix is already present.
2. Write/verify the `ServerBase` accept-filter regression test first.
3. Run only the relevant server-base test target.
4. Implement or confirm the accept-filter fix.
5. Add `HttpClient` HTTPS rejection tests.
6. Implement explicit scheme rejection.
7. Add `HttpResponseParser` strictness tests.
8. Implement parser strictness if tests fail.
9. Only then decide whether there is enough time to tackle `HttpFileServer` large-file efficiency.

## Verification Checklist

Use this before ending the next coding session:

- Relevant changed tests pass.
- No unrelated files were reverted.
- Public behavior changes are reflected in tests.
- If `HttpClient` errors changed, error message assertions are stable and intentional.
- If `HttpFileServer` work started, the chosen direction and limitations are documented.

## Current Dirty Worktree To Review First

At the time this handoff was written, the repository had uncommitted changes in at least these files:

- `examples/advanced_file_server.cpp`
- `lib/CMakeLists.txt`
- `lib/include/HttpPollServer.h`
- `lib/include/ServerBase.h`
- `lib/src/HttpPollServer.cpp`
- `tests/CMakeLists.txt`
- `lib/include/AccessLogger.h` (new)
- `lib/include/IpFilter.h` (new)
- `lib/src/AccessLogger.cpp` (new)
- `lib/src/IpFilter.cpp` (new)
- `tests/test_access_logger.cpp` (new)
- `tests/test_ip_filter.cpp` (new)

Important note:

- These changes were present before writing this plan.
- They appear related to IP filtering, access logging, and the accept-filter lifecycle.
- They should be reviewed as part of the next session instead of overwritten blindly.

## Suggested New-Session Prompt

Use something close to this to restart efficiently:

"Read `plan.md` first, then inspect the current uncommitted changes. Verify whether the accept-filter lifecycle bug is already fixed in the working tree, then add the missing regression tests for accept-filter rejection, HTTPS handling in `HttpClient`, and malformed `HttpResponse` status-line/version parsing. Do not revert unrelated user changes."

## Success Criteria For The Next Session

- The accept-filter lifecycle is provably safe via regression tests.
- `HttpClient` has explicit and tested scheme handling.
- Response parser strictness is clarified and tested.
- A concrete direction is chosen for `HttpFileServer` large-file efficiency.
- Existing user changes in the worktree are preserved unless explicitly replaced.

## TLS/HTTPS Readiness Plan (OpenSSL, Feature-Flagged)

This section captures an implementation-ready direction for optional HTTPS support while preserving current plain-HTTP defaults.

Important terminology note:

- For HTTPS, use OpenSSL (TLS library), not OpenSSH (SSH stack).

### Goal

- Add optional TLS to server and client paths without destabilizing existing poller architecture.
- Keep non-TLS builds dependency-free and behavior-identical.
- Prefer virtual extension points in `HttpPollServer` for TLS I/O and handshake lifecycle.

### Proposed Feature Flag

- Add `AISOCKS_ENABLE_TLS` CMake option, default `OFF`.
- When `OFF`:
  - No OpenSSL dependency.
  - Existing behavior unchanged.
  - `HttpClient` continues returning explicit HTTPS-not-supported error.
- When `ON`:
  - Link OpenSSL (`OpenSSL::SSL`, `OpenSSL::Crypto`).
  - Enable HTTPS server/client paths and TLS tests.

Suggested CMake integration points:

- top-level `CMakeLists.txt`: `option(AISOCKS_ENABLE_TLS "Enable TLS/HTTPS via OpenSSL" OFF)`
- `lib/CMakeLists.txt`:
  - conditional `find_package(OpenSSL REQUIRED)`
  - conditional link libraries
  - `target_compile_definitions(aiSocksLib PUBLIC AISOCKS_ENABLE_TLS)`
- `CMakePresets.json`: add a `tls-debug` preset.

### Architecture Constraints Observed

- `HttpPollServer::onReadable()` and `onWritable()` are `final`, so TLS cannot be introduced by overriding these methods directly in derived classes.
- `Socket` and `SocketImpl` data-transfer methods are not virtual, so replacing transport behavior below `TcpSocket` is not a low-friction path.
- `ServerBase` stores clients as `std::unique_ptr<TcpSocket>`, so introducing a polymorphic socket family is expensive and invasive.

Implication:

- The least invasive design is to keep `TcpSocket` as the transport and add TLS adaptation inside `HttpPollServer` via virtual hooks called from existing `final` methods.

### Proposed Virtual Hook Surface in `HttpPollServer`

Add protected virtual methods in `HttpPollServer`:

- `virtual int socketRead(TcpSocket& sock, HttpClientState& s, char* buf, int len);`
- `virtual int socketWrite(TcpSocket& sock, HttpClientState& s, const char* data, int len);`
- `virtual ServerResult doHandshakeStep(TcpSocket& sock, HttpClientState& s);`
- `virtual bool isTlsMode(const HttpClientState& s) const;`

Base defaults (plain HTTP):

- `socketRead` -> `sock.receive(...)`
- `socketWrite` -> `sock.sendChunked(...)`
- `isTlsMode` -> `false`
- `doHandshakeStep` -> not used in base (safe fallback)

`HttpPollServer::onReadable()` / `onWritable()` behavior change:

- Add handshake gate near function start:
  - if TLS mode and handshake incomplete, run `doHandshakeStep(...)` and return its result.
- Replace direct socket read/write calls with `socketRead(...)` / `socketWrite(...)`.

### TLS Session State Placement

Add TLS state to `HttpClientState` under `#ifdef AISOCKS_ENABLE_TLS`:

- `std::unique_ptr<TlsSession> tls;`
- `bool tlsHandshakeDone{false};`

`TlsSession` responsibilities:

- own `SSL*` lifetime for one client connection
- perform non-blocking `SSL_accept` state machine
- map `SSL_ERROR_WANT_READ/WRITE` to event-loop-friendly behavior
- provide `read()` / `write()` wrappers for record I/O

### New Server Types

- `HttpsPollServer : public HttpPollServer`
  - override handshake + I/O virtual hooks
  - initialize per-client TLS state in `onClientConnected`
- `HttpsFileServer : public HttpFileServer`
  - same TLS overrides plus inherited file-serving behavior

Note on inheritance design:

- Avoid multiple-inheritance diamond (`HttpsPollServer` + `HttpFileServer`).
- Keep `HttpsFileServer` deriving from `HttpFileServer` and implement TLS overrides there (or via shared helper).

### HttpClient HTTPS Plan

When TLS flag is ON:

- For `https://` URLs, create TCP socket, then run `SSL_connect`, then send/receive through TLS session.
- Preserve existing redirect semantics; redirects to HTTPS should use the same explicit TLS path.

When TLS flag is OFF:

- Keep deterministic failure message for `https://`.

### Certificate Scope for Initial Implementation

- Server: PEM cert/key file loading only.
- Client: baseline TLS handshake support; advanced CA policy and pinning can be phase 2.
- Keep initial certificate policy explicit in docs/tests to avoid ambiguous security posture.

### Test Plan for TLS Integration

Add conditional tests (only when TLS enabled):

- basic HTTPS handshake and request/response for `HttpsPollServer`
- `HttpsFileServer` serves file content over TLS
- handshake progresses non-blocking across read/write readiness cycles
- certificate load failure path returns clear error
- connection teardown does not leak poller registrations/state

Also keep non-TLS tests unchanged to verify feature-flag isolation.

### Recommended Delivery Order

1. Add feature flag and conditional build wiring.
2. Add TLS context/session internals.
3. Add `HttpPollServer` virtual hook surface and handshake gate.
4. Add `HttpsPollServer` and TLS tests.
5. Add `HttpsFileServer` and file-serving TLS tests.
6. Add `HttpClient` HTTPS path (still flag-gated).

### Acceptance Criteria for TLS Plan

- Non-TLS build remains default and fully backward-compatible.
- TLS build compiles cleanly and links only when opted in.
- HTTPS server path works without blocking event loop during handshake.
- TLS integration uses overridable behavior in `HttpPollServer` rather than poller redesign.
- Existing code paths for plain HTTP remain measurable and unchanged when TLS is disabled.