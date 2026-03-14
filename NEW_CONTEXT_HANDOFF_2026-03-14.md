# Context Handoff - AISocks review (2026-03-14)

## Scope reviewed
- HttpClient with TLS enabled and disabled.
- HttpPollServer and HttpFileServer behavior.
- advanced_file_server example behavior.

## High-priority findings

1. HttpClient connection cache key omits scheme.
- Current reuse key is host+port only.
- Risk: HTTP and HTTPS can cross-reuse the wrong cached socket/session in TLS builds.
- Fix direction: include scheme in cache key and only reuse TLS session for HTTPS cache entries.

2. HttpClient reused-connection retry logic can fail to reconnect on read/parse errors.
- Some retry paths clear cache and continue inner loop instead of forcing reconnect.
- Risk: retry may keep using a bad socket and fail instead of transparently reconnecting.
- Fix direction: break to outer reconnect path or perform explicit reconnect in those branches.

## Medium findings

3. URL authority port parsing accepts partial numeric values.
- Values like 443abc may parse as 443.
- Fix direction: require full-string numeric consumption and strict range checks.

4. TLS handshake is not clearly bounded by request timeout.
- Request timeout deadline is set after handshake setup path.
- Risk: stalled handshake can exceed expected request timeout.
- Fix direction: apply explicit handshake deadline or nonblocking timeout handling.

## Server/example findings

5. advanced_file_server CLI port parsing uses unchecked atoi cast to uint16.
- Negative and overflow inputs can wrap to unexpected port.
- Fix direction: strict parse and validation for 1..65535.

6. advanced_file_server access.log tail endpoint reads full log into memory each request.
- Can spike memory and latency with large logs.
- Fix direction: seek from file end and read only enough bytes for requested tail lines.

## Notes
- Redirect handling currently tracks 301, 302, 307, 308.
- No code changes were made in this review pass.
