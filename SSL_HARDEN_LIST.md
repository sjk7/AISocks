# HTTPS Production Readiness Checklist

Status target: internet-facing production deployment of `HttpsPollServer`

## 1. TLS Handshake Hardening

- [x] Add an explicit TLS handshake timeout separate from HTTP slowloris protection.
- [x] Ensure stalled handshakes are dropped promptly even before any HTTP bytes arrive.
- [x] Log handshake failures with actionable OpenSSL error details.
- [ ] Add tests for:
  - [x] client connects and never completes TLS handshake
  - [x] repeated `SSL_ERROR_WANT_READ` / `SSL_ERROR_WANT_WRITE`
  - [x] handshake timeout under idle and loaded conditions
 - [x] Add an explicit TLS handshake timeout separate from HTTP slowloris protection.
 - [x] Ensure stalled handshakes are dropped promptly even before any HTTP bytes arrive.
 - [x] Log handshake failures with actionable OpenSSL error details.
 - [x] Add tests for:
   - [x] client connects and never completes TLS handshake
  - [x] repeated `SSL_ERROR_WANT_READ` / `SSL_ERROR_WANT_WRITE`
  - [x] handshake timeout under idle and loaded conditions

Relevant code:
- [lib/include/HttpsPollServer.h](lib/include/HttpsPollServer.h#L46)
- [lib/include/HttpsPollServer.h](lib/include/HttpsPollServer.h#L64)
- [lib/src/HttpPollServer.cpp](lib/src/HttpPollServer.cpp#L551)

## 2. Server TLS Policy

- [ ] Make server-side TLS policy configurable through `TlsServerConfig`.
- [ ] Configure allowed protocol versions explicitly.
- [ ] Configure TLS 1.2 cipher list.
- [ ] Configure TLS 1.3 ciphersuites.
- [ ] Decide and document server cipher preference policy.
- [x] Disable legacy or weak options beyond the current SSLv2/SSLv3 ban.
- [x] Consider explicit OpenSSL security level expectations per platform.
 - [x] Make server-side TLS policy configurable through `TlsServerConfig`.
 - [x] Configure allowed protocol versions explicitly.
 - [x] Configure TLS 1.2 cipher list.
 - [x] Configure TLS 1.3 ciphersuites.
 - [x] Decide and document server cipher preference policy.
 - [x] Make server-side TLS policy configurable through `TlsServerConfig`.
 - [x] Configure allowed protocol versions explicitly.
 - [x] Configure TLS 1.2 cipher list.
 - [x] Configure TLS 1.3 ciphersuites.
 - [x] Decide and document server cipher preference policy.
 - [x] Disable legacy or weak options beyond the current SSLv2/SSLv3 ban.
 - [x] Consider explicit OpenSSL security level expectations per platform.
 - [x] Disable legacy or weak options beyond the current SSLv2/SSLv3 ban.
 - [x] Consider explicit OpenSSL security level expectations per platform.

Relevant code:
- [lib/src/TlsOpenSsl.cpp](lib/src/TlsOpenSsl.cpp#L103)
- [lib/src/TlsOpenSsl.cpp](lib/src/TlsOpenSsl.cpp#L104)

## 3. Certificate and Identity Features

 - [x] Support SNI-based certificate selection if multiple hostnames are expected.
 - [x] Support hot certificate reload or clearly document restart-only certificate rotation.
- [x] Validate certificate/key loading failures with more context in logs.
- [ ] Decide whether chain file validation should reject incomplete deployments earlier.

Relevant code:
- [lib/include/HttpsPollServer.h](lib/include/HttpsPollServer.h#L101)

## 4. Client Authentication

- [ ] Decide whether mTLS is required for any deployments.
- [ ] If yes, add configurable client certificate verification.
- [ ] Support CA file / CA directory configuration for client-cert validation.
- [ ] Support optional vs required client certificate modes.
- [ ] Expose verified peer certificate details to application code if needed.
 - [x] Add tests for accepted, rejected, and missing client certificates.

-- Implemented status:
- [ ] Decide whether mTLS is required for any deployments.
- [x] If yes, add configurable client certificate verification.
- [x] Support CA file / CA directory configuration for client-cert validation.
- [x] Support optional vs required client certificate modes.
- [x] Expose verified peer certificate details to application code if needed.
- [ ] Add tests for accepted, rejected, and missing client certificates.

## 5. Revocation and Trust Model

- [ ] Decide whether revocation checking is required in-process.
- [ ] If yes, implement OCSP / CRL policy or explicitly terminate TLS upstream instead.
- [ ] Document the trust model for public deployment.

Relevant docs:
- [README.md](README.md#L44)

## 6. ALPN and Protocol Negotiation

- [ ] Decide whether HTTP/1.1-only is acceptable.
- [ ] If yes, explicitly document ALPN behavior or lack of ALPN.
- [ ] If no, implement ALPN negotiation.
- [ ] Add tests for clients that expect negotiated protocol behavior.

## 7. TLS Shutdown Semantics

- [ ] Decide whether to send graceful TLS `close_notify`.
- [ ] Implement orderly TLS shutdown where appropriate.
- [ ] Confirm behavior on keep-alive close, server stop, timeout, and error paths.
- [ ] Add tests for clean TLS close vs abrupt disconnect.

## 8. Session Resumption and Tickets

- [ ] Decide whether new-connection session resumption is required.
- [ ] If yes, configure session cache and/or tickets deliberately.
- [ ] If no, document that choice clearly.
- [ ] Add tests for session reuse expectations if the feature is enabled.

Relevant docs:
- [README.md](README.md#L49)

## 9. Observability

- [ ] Add structured logging for:
  - [ ] TLS init failure
  - [ ] certificate load failure
  - [x] handshake failure
  - [ ] protocol/cipher negotiated
  - [ ] client-cert verification result if enabled
- [ ] Add counters/metrics for:
  - [ ] handshake success/failure
  - [ ] timeout disconnects
  - [ ] TLS protocol versions
  - [ ] ciphers negotiated
- [ ] Ensure logs do not leak sensitive material.

## 10. Abuse Resistance

- [ ] Review accept-path behavior under large numbers of stalled TLS connections.
- [ ] Add limits or heuristics for handshake-phase resource abuse.
- [ ] Confirm keep-alive timeout and slowloris settings are appropriate for public internet exposure.
- [ ] Add load tests with hostile clients, not just normal traffic.

Relevant code:
- [lib/include/HttpPollServer.h](lib/include/HttpPollServer.h#L246)
- [lib/include/ServerBase.h](lib/include/ServerBase.h#L311)

## 11. Testing Gaps

- [x] Add server-focused TLS tests, not only client integration tests.
- [x] Add tests for:
  - [x] handshake timeout
  - [x] handshake failure logging
  - [x] certificate rotation behavior
  - [ ] mTLS if added
  - [ ] ALPN if added
  - [ ] graceful TLS shutdown
  - [x] concurrent hostile connection patterns
- [ ] Run these in CI with TLS enabled on all supported platforms.

Current evidence:
- [tests/test_server_launchpad.cpp](tests/test_server_launchpad.cpp#L152)
- [tests/test_tls_client.cpp](tests/test_tls_client.cpp)

## 12. Deployment Decision

- [ ] Decide whether `HttpsPollServer` is meant to be:
  - [ ] a direct internet-facing TLS endpoint
  - [ ] an internal/server-side component behind nginx/Caddy/HAProxy/Envoy/load balancer
- [ ] If internet-facing, complete every checklist section above.
- [ ] If behind a hardened TLS terminator, document that as the recommended production architecture.

## Recommended Short-Term Position

- [ ] Treat current `HttpsPollServer` as suitable for controlled/internal use.
- [ ] Do not market it as fully hardened public-edge HTTPS yet.
- [ ] Recommend external TLS termination for current production deployments.