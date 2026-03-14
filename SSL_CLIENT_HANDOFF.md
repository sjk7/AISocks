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

- [OPEN][ITER-1] Revocation strategy (OCSP/CRL) scope decision and docs:
  - Decision target for this branch: keep revocation checks OFF by default and document this explicitly.
  - Add options shape proposal (no implementation yet):
    - enableOcspStaplingCheck (best-effort),
    - crlFile / crlDir,
    - hardFailOnRevocationUnavailable.
  - Document platform caveats (OpenSSL does not do full online revocation by default).
  - Exit criteria: README + SANITIZERS/TEST docs describe threat model and operator expectations.
- [OPEN][ITER-2] Session resumption across new TCP connections:
  - Current behavior is unchanged: only keep-alive reuse on the same live socket.
  - Planned implementation:
    - per-HttpClient in-memory SSL session cache keyed by scheme+host+port.
    - cache invalidation when setOptions() changes trust/verify knobs.
    - conservative defaults (small bounded cache, no cross-process persistence).
  - Exit criteria: second fresh TCP connection to same origin attempts resume, plus safe fallback on miss/failure.

## Remaining Tests

- No remaining TLS test gaps from this handoff plan.

## Next Iteration Order

1. Re-evaluate whether ITER-2 (session resumption across new TCP connections) should start in this branch or a follow-up branch.
2. Finalize ITER-1 revocation scope text in docs before handoff close.

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

## Current Key Touchpoints

- lib/include/HttpClient.h
- lib/include/TlsOpenSsl.h
- lib/src/TlsOpenSsl.cpp
- tests/test_tls_client.cpp

## Validation Commands

- cmake --preset tls-relwithdebinfo
- cmake --build --preset tls-relwithdebinfo -j
- ctest --test-dir build-tls-relwithdebinfo -j8 --output-on-failure
