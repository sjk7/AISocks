# SSL Client Handoff (Current Context)

Date: 2026-03-14
Branch: secure
Scope: HttpClient TLS client path

## Workflow Rule

- Do not push until ALL tests pass, including non-SSL and TLS test suites.

## Completed

- [DONE] HTTPS client path behind AISOCKS_ENABLE_TLS.
- [DONE] Strict verification path when verifyCertificate=true:
  - chain verification.
  - hostname/IP verification setup before handshake.
  - post-handshake verify-result check.
  - peer certificate presence check.
- [DONE] Default TLS verification policy:
  - HttpClient verifyCertificate now defaults to true.
- [DONE] Trust-store configuration:
  - caCertFile only.
  - caCertDir only.
  - caCertFile + caCertDir.
  - deterministic invalid file/dir setup errors.
- [DONE] Host verification hardening:
  - trailing-dot hostname normalization.
  - robust IP-literal detection.
  - non-ASCII DNS host rejection with punycode guidance.
- [DONE] SNI policy:
  - DNS hosts send SNI.
  - IP literals do not send SNI.
- [DONE] Configurable verify depth:
  - verifyDepth in options (`-1` OpenSSL default, `>=0` explicit depth).
  - invalid depth validation.
- [DONE] SSL_CTX reuse:
  - reused per HttpClient instance.
  - rebuilt on setOptions() change.
- [DONE] IPv6 verification coverage:
  - ::1 SAN match success.
  - IPv6 SAN mismatch failure.
- [DONE] CA source matrix positive cases:
  - CA directory-only success fixture using deterministic hashed capath temp dir.
  - valid file+dir success fixture.
- [DONE] Redirect + TLS verification interaction coverage:
  - HTTPS redirect to different host succeeds with verify enabled and explicit trust roots.
  - HTTPS->HTTP downgrade follow behavior is explicitly tested and asserted.
  - redirectChain/finalUrl assertions lock observable behavior.
- [DONE] Default system-roots behavior test (gated):
  - Added `AISOCKS_RUN_SYSTEM_ROOT_TLS_TEST=1` environment-gated smoke test.
  - Default behavior is SKIP to keep CI deterministic and non-networked.

## Open Work

- [DEFERRED][FOLLOW-UP] Session resumption across new TCP connections:
  - Current branch behavior remains unchanged: only keep-alive reuse on the same live socket.
  - Re-evaluation outcome: do not ship new-connection resumption in this branch.
  - Reason: deterministic proof was not stable on the current OpenSSL/TLS stack because resumable state delivery depends on post-handshake ticket timing.
  - Follow-up branch scope if revived:
    - per-HttpClient in-memory SSL session cache keyed by scheme+host+port.
    - cache invalidation when setOptions() changes trust/verify knobs.
    - deterministic TLS 1.3 ticket-harvest strategy and cross-platform proof.

## Remaining Tests

- No remaining TLS test gaps from this handoff plan.

## Next Iteration Order

1. No further in-branch TLS client work planned from this handoff.
2. If session resumption is revisited, do it in a follow-up branch with deterministic TLS 1.3 proof.

## Latest Validation

- Rebuilt and ran `build-tls-relwithdebinfo/tests/test_tls_client` after adding P1 tests.
- New P1 tests passed:
  - `HttpClient HTTPS verify enabled succeeds with CA dir only`
  - `HttpClient HTTPS verify enabled succeeds with CA file plus valid CA dir`
- Added guard in IPv6 SAN-match test to avoid abort-on-failure behavior and emit actionable failure text.
- Re-ran `build-tls-relwithdebinfo/tests/test_tls_client`: 60 passed, 0 failed.
- Ran non-SSL CMake suite via CTest (`build-relwithdebinfo`): all discovered tests passed (48/48).
- Added and validated P3 redirect tests:
  - `HttpClient follows HTTPS redirect to different host with verify enabled`
  - `HttpClient follows HTTPS to HTTP redirect and reports final URL chain`
- Added and validated gated P2 system-roots smoke test:
  - `HttpClient HTTPS default system roots smoke test (gated)`
  - default run behavior is SKIP unless `AISOCKS_RUN_SYSTEM_ROOT_TLS_TEST=1`.
- Current TLS integration run: 76 passed, 0 failed.
- Non-SSL CMake suite re-run via CTest (`build-relwithdebinfo`): all discovered tests passed.
- Finalized revocation scope docs in `README.md` and `README_TESTS.md`:
  - revocation remains OFF by default.
  - operator expectation is explicit trust-store + hostname verification, not OCSP/CRL enforcement.
- Re-evaluated new-connection TLS session resumption and deferred it from this branch due non-deterministic TLS 1.3 ticket timing in proof tests.

## Current Key Touchpoints

- lib/include/HttpClient.h
- lib/include/TlsOpenSsl.h
- lib/src/TlsOpenSsl.cpp
- tests/test_tls_client.cpp

## Validation Commands

- cmake --preset tls-relwithdebinfo
- cmake --build --preset tls-relwithdebinfo -j
- ctest --test-dir build-tls-relwithdebinfo -j8 --output-on-failure
