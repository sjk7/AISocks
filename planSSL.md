# SSL/TLS Remaining Work

Current branch status as of 2026-03-14:

- Accept-filter lifecycle safety is implemented and regression-tested.
- HttpClient scheme handling, strict authority-port parsing, and TLS handshake timeout bounding are implemented and tested.
- HttpResponseParser status-line/version strictness is implemented and tested.
- TLS feature-flag wiring exists, and the TLS macOS preset builds and runs the full test suite.

## Remaining Actionable Items

### 1. HttpFileServer large-file hot path

Status: Not complete

What remains:
- Reduce large-response copy and allocation cost.
- Avoid building one monolithic header+body string for large files.
- Decide on one of:
  - streamed header + file body
  - sendfile-based path
  - threshold-based hybrid

Current evidence:
- HttpFileServer still reads file content into memory and builds a single response buffer.
- See lib/src/HttpFileServer.cpp.

Suggested acceptance criteria:
- Small-file behavior stays unchanged.
- HEAD and keep-alive behavior remain correct.
- Large-file path has focused tests and/or benchmark evidence.

### 2. Dedicated HTTPS server types

Status: Optional design item, not implemented

What remains:
- Decide whether the codebase actually wants explicit HttpsPollServer and HttpsFileServer types.

Current state:
- TLS support is implemented via HttpPollServer TLS hooks rather than separate concrete HTTPS server classes.
- Tests already cover TLS server/client behavior using hook-based server implementations.

Decision needed:
- If named HTTPS server types are part of the intended public API, implement them.
- If the hook-based design is the intended API, this item can be dropped permanently.

## Deletable Once

This file can be deleted once:
- the HttpFileServer large-file path decision is implemented or explicitly deferred elsewhere, and
- the dedicated HTTPS server type question is either implemented or intentionally closed.
