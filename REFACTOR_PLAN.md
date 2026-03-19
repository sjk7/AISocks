# AISocks Refactor Plan

## Goal
Reduce maintenance complexity in core networking and HTTP/TLS paths without changing external behavior.

- [x] Make certain our server and client classes are not copiable,
  but moveable is OK.

- [x] Add noexcept to high-impact / hot areas of code where we KNOW
	an exception is not thrown.

## Why This Plan
Complexity is concentrated in a few engine files:
- `lib/src/SocketImpl.cpp`
- `lib/src/HttpPollServer.cpp`
- `lib/src/TlsOpenSsl.cpp`
- `lib/include/ServerBase.h`

Most other modules are already relatively simple and cohesive.

## Non-Goals
- No protocol feature changes.
- No public API breakage unless explicitly approved.
- No broad style-only rewrites.

## Baseline Before Refactor
1. Run existing test suite and save baseline pass/fail + timing.
2. Capture current behavior for:
- HTTP keep-alive and pipelining
- Chunked request/response framing
- TLS handshake and ALPN
- Connect timeouts + DNS timeout behavior
3. Keep this baseline for regression checks after each phase.

## Phase 1: Centralize HTTP Framing Logic (Low Risk, High Return)
### Scope
- Extract shared request framing/parser helpers used by both:
- `lib/src/HttpPollServer.cpp`
- `lib/src/HttpRequest.cpp`

### Changes
- Introduce a single internal component (example: `HttpRequestFramer`) for:
- Content-Length validation
- Transfer-Encoding: chunked detection
- Chunk-size parsing
- Decoding/consumption boundaries

### Benefits
- Removes duplicate logic and drift risk.
- Makes parser behavior easier to reason about and test.

### Exit Criteria
- No duplicate chunked/content-length framing code in the two files.
- Existing parser tests pass, plus focused new tests for edge cases.

## Phase 2: Split HttpPollServer State Machine (Medium Risk, High Return)
### Scope
Refactor `lib/src/HttpPollServer.cpp` by separating major lifecycle stages.

### Changes
- Break `onReadable` and `onWritable` into explicit internal stages:
- TLS handshake stage
- Read/buffer stage
- Request-frame inspection stage
- Response-build stage
- Send/stream stage
- Pipeline continuation stage

### Benefits
- Smaller functions, fewer intertwined branches.
- Easier to test each stage independently.

### Exit Criteria
- `onReadable` and `onWritable` are orchestration-only wrappers.
- Stage helpers have focused responsibilities and tests.

## Phase 3: Decompose SocketImpl by Concern (Medium-High Risk, Highest Return)
### Scope
Refactor `lib/src/SocketImpl.cpp` into focused internal units.

### Changes
- Extract connect path into a separate internal module:
- DNS worker gating
- timed connect wait loop
- event-fd setup/teardown
- Keep send/recv loops in a dedicated transfer module.
- Keep option getters/setters in an options-focused module.

### Benefits
- Reduces one of the largest maintenance hotspots.
- Improves portability debugging by isolating platform-specific paths.

### Exit Criteria
- `SocketImpl.cpp` no longer holds all connect + transfer + option logic in one file.
- No behavior regressions in timeout/DNS/connect tests.

## Phase 4: Make TLS Policy Application Data-Driven (Medium Risk)
### Scope
Refactor policy setup in `lib/src/TlsOpenSsl.cpp`.

### Changes
- Introduce a policy struct/builder and one apply function for:
- protocol min/max
- ciphers/ciphersuites
- verify mode/depth
- default CA vs explicit CA file/dir
- ALPN setup

### Benefits
- Reduces branching in one large function.
- Makes TLS settings easier to audit and unit test.

### Exit Criteria
- TLS policy decisions are represented in data before application.
- Existing TLS tests still pass, including ALPN + cert validation paths.

## Phase 5: Simplify HttpClientState Ownership Rules (Targeted Risk Reduction)
### Scope
Improve safety around `responseBuf` and `responseView` coupling in `lib/include/HttpPollServer.h`.

### Changes
- Replace fragile view/owner coupling with a clearer response payload model.
- Prefer move-only semantics where practical for per-connection state.

### Benefits
- Fewer lifetime/pointer-fixup hazards.
- Safer future changes in response streaming and buffering.

### Exit Criteria
- No manual pointer fixup logic needed after move/copy in state type.
- Pipeline + streaming tests remain green.

## Phase 6: Thin ServerBase Orchestration (Optional, After Core Phases)
### Scope
Refactor `lib/include/ServerBase.h` to keep it coordinator-focused.

### Changes
- Move timeout sweep policy and accept throttling mechanics into helper policy objects.
- Keep `ServerBase` focused on event dispatch and lifecycle wiring.

### Benefits
- Lower template/header complexity.
- Better separation between orchestration and policy.

### Exit Criteria
- `ServerBase` primarily coordinates loop + hooks.
- Timeout and accept policies are isolated units.

## Test Strategy Per Phase
For every phase:
1. Run targeted tests first (fast feedback).
2. Run broad suite before merge.
3. Compare behavior and performance against baseline.
4. If regressions appear, revert only that phase and narrow scope.

## Recommended Execution Order
1. Phase 1
2. Phase 2
3. Phase 3
4. Phase 4
5. Phase 5
6. Phase 6 (optional)

## Suggested First PR
Start with Phase 1 only:
- Small blast radius
- Easy review
- Immediate reduction of duplicate HTTP framing logic
- Builds confidence for later higher-risk phases
