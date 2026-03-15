# SSL/TLS Remaining Work

Current branch status as of 2026-03-14:

- Accept-filter lifecycle safety is implemented and regression-tested.
- HttpClient scheme handling, strict authority-port parsing, and TLS handshake timeout bounding are implemented and tested.
- HttpResponseParser status-line/version strictness is implemented and tested.
- TLS feature-flag wiring exists, and the TLS macOS preset builds and runs the full test suite.

## Remaining Actionable Items

### 1. HttpFileServer large-file hot path

Status: Implemented

Implemented:
- Threshold-based hybrid path retained.
- Large-file GET responses now send headers first and stream the file body in fixed-size chunks from the server write loop (no monolithic header+body allocation).
- Small files still use existing in-memory/cache path.

Current evidence:
- Streamed response wiring and write-loop support: `lib/include/HttpPollServer.h`, `lib/src/HttpPollServer.cpp`.
- Large-file server path now uses streamed file response setup: `lib/src/HttpFileServer.cpp`.
- Regression test coverage (large-file hot path): `tests/test_advanced_file_server.cpp` (`testLargeFileBypassesCacheForHotPath`).

Behavioral notes:
- HEAD semantics are preserved (headers only, with correct `Content-Length`).
- Keep-alive behavior remains unchanged.

### 2. Dedicated HTTPS server types

Status: Implemented

Current state:
- Named HTTPS server types are present: `HttpsPollServer` and `HttpsFileServer`.
- TLS support still uses HttpPollServer hook points under those concrete HTTPS wrappers.

## Deletable Once

This file can be deleted once:
- the above status notes are no longer needed as historical context.
