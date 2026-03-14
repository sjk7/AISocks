# SSL Client Handoff (Remaining Work)

Date: 2026-03-14
Branch: secure
Scope: HttpClient TLS client path

## What is already done

- HTTPS client path is implemented behind AISOCKS_ENABLE_TLS.
- Strict verification is now wired when verifyCertificate=true:
  - Chain verification via OpenSSL verify mode.
  - Hostname/IP verification setup before handshake.
  - Post-handshake verify-result check.
  - Peer certificate presence check.
- verifyCertificate=false behavior remains permissive and unchanged.
- TLS integration tests cover:
  - verify enabled + trusted custom CA + matching host succeeds.
  - verify enabled + hostname mismatch fails.
  - verify enabled + untrusted self-signed fails.
- Trust-store API now supports CA file and/or CA directory:
  - `caCertFile` only.
  - `caCertDir` only.
  - `caCertFile + caCertDir` together.
  - neither (system defaults).
- Deterministic trust-source setup checks are enforced before OpenSSL load:
  - missing CA file is a setup error.
  - missing or non-directory CA dir is a setup error.

## Remaining SSL client work

### Status snapshot (as of 2026-03-14)

- [DONE] P0.2 trust-store API supports CA file/dir and deterministic invalid-path behavior.
- [DONE] P1.4 SNI for IP literals is disabled; DNS hosts still send SNI.
- [DONE] P1.3 host normalization + IDN policy: trailing-dot strip, robust IP literal checks, and non-ASCII DNS host rejection with punycode guidance under verify mode.
- [DONE] P0.1 verifyCertificate default-policy decision.
- [DONE] P1.5 verify depth option.
- [OPEN] P1.6 revocation strategy.
- [DONE] P2.7 SSL_CTX reuse per HttpClient instance.
- [OPEN] P2.8 session resumption across new TCP connections.

### P0: Security posture defaults and API ergonomics

1. Decide production default for verifyCertificate.
- [DONE] runtime default is now `verifyCertificate=true`.

2. Improve trust-store API beyond single file. [DONE]
- CA directory support is implemented (`caCertDir`) with deterministic precheck
  errors for invalid file/dir inputs.
- Remaining: add a positive CA-directory-only integration test fixture that is
  portable across CI environments.

### P1: Hostname verification edge hardening

3. Normalize host before OpenSSL hostname checks. [DONE]
- Done:
  - strip trailing dots before OpenSSL host/IP verify setup.
  - use robust IP literal detection via `Socket::isValidIPv4/isValidIPv6`.
- IDN/punycode policy: callers must pass punycode (A-label) hostnames when verification is enabled; non-ASCII DNS hosts are rejected with a clear error.

4. SNI behavior for IP literals. [DONE]
- SNI is now sent for DNS hosts only; skipped for IP literals.
- Added integration coverage:
  - DNS host path sends SNI.
  - IP-literal host path does not send SNI.

### P1: Verification depth and revocation

5. Add configurable verify depth.
- [DONE] `verifyDepth` is exposed in HttpClient options (`-1` = OpenSSL
  default, `>=0` applies explicit depth).
- Added test coverage for invalid depth validation and `verifyDepth=0`
  success path with trusted leaf.

6. Evaluate revocation strategy.
- Not currently checking OCSP/CRL.
- If threat model requires it, add optional revocation checks and document operational requirements.

### P2: Performance and lifecycle cleanup

7. Reuse client SSL_CTX across requests.
- [DONE] TLS context is reused per HttpClient instance and rebuilt when
  `setOptions()` changes trust/verify settings.

8. Session resumption policy.
- Current keep-alive reuses live TLS session on same socket only.
- No resumed handshakes across new TCP connections.
- Optional future: OpenSSL client session cache/tickets per HttpClient instance.

## Remaining tests to add

1. IPv6 hostname/IP verification paths
- [::1] certificate SAN IP match succeeds with verify enabled. [DONE]
- mismatch for IPv6 IP literal fails. [DONE]

2. CA source matrix tests
- default system roots path behavior (environment-dependent, may need containerized fixture).
- custom CA file invalid path produces clear setup error. [DONE]
- custom CA dir invalid path produces clear setup error. [DONE]
- file+dir with invalid dir fails setup deterministically. [DONE]
- add portable positive directory-only and valid file+dir success tests.

3. SNI policy tests
- DNS host sends SNI and succeeds. [DONE]
- IP literal path behavior after SNI policy decision remains compatible. [DONE]

4. Redirect + TLS verification interactions
- HTTPS redirect to different host with verify enabled (expected re-verify on new host).
- HTTPS to HTTP downgrade handling policy remains explicit and tested.

## Current key touchpoints

- HttpClient TLS handshake and verify logic:
  - lib/include/HttpClient.h
- TLS context verify configuration:
  - lib/include/TlsOpenSsl.h
  - lib/src/TlsOpenSsl.cpp
- TLS integration tests:
  - tests/test_tls_client.cpp

## Suggested next implementation order

1. Revisit default verifyCertificate policy and update docs/changelog.

## Validation command set

- cmake --preset tls-relwithdebinfo
- cmake --build --preset tls-relwithdebinfo -j
- ctest --test-dir build-tls-relwithdebinfo -j8 --output-on-failure
