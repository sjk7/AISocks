# Hardening + Protocol Fix Plan (12 Mar 2026)

Scope agreed from latest review feedback:
- Keep items 1-4 from review.
- Do NOT treat detached DNS timeout thread as a defect (accepted design tradeoff for bounded connect timeout with blocking DNS APIs).
- Implement hardening/profile-level limits and predictable close semantics.

---

## Goals

1. Remove protocol surprises in request-target and redirect resolution.
2. Make malformed-request behavior explicit and deterministic (close semantics).
3. Add global parser/body limits aligned with defensive production defaults.
4. Reduce avoidable string churn in frequently used redirect/path handling.
5. Add tests for each behavior change before push.

---

## Proposed Fixes

### A) Explicit close semantics for malformed requests

Files:
- lib/src/HttpPollServer.cpp
- lib/src/HttpFileServer.cpp

Changes:
- In HttpPollServer::dispatchBuildResponse, parse once and if invalid:
  - emit 400 response directly,
  - force closeAfterSend = true,
  - include Connection: close.
- Ensure method-rejection paths (405) also carry deterministic connection behavior.
- Keep derived buildResponse implementations from needing to infer parser validity.

Tests:
- Add/extend test in tests/test_http_poll_server.cpp:
  - malformed request returns 400,
  - response includes Connection: close,
  - second request on same socket fails (server closed).

Acceptance:
- No malformed request response is sent on persistent keep-alive connection.

---

### B) Strict request-target form validation (RFC-consistent)

Files:
- lib/src/HttpRequest.cpp
- tests/test_http_request.cpp

Changes:
- Enforce method-target matrix explicitly:
  - CONNECT: authority-form only.
  - OPTIONS *: only asterisk-form.
  - non-CONNECT: reject authority-form.
  - CONNECT absolute-form should be rejected.
- Keep current control-char and query-size checks.

Tests:
- Add cases:
  - CONNECT host:443 is valid.
  - CONNECT http://host/path is invalid.
  - GET host:443 (authority-form) is invalid.
  - OPTIONS * valid, OPTIONS /path valid, OPTIONS *?x invalid.

Acceptance:
- Parser behavior is deterministic for all 4 request-target forms.

---

### C) Redirect resolution correctness + reduced path churn

Files:
- lib/include/HttpClient.h
- tests/test_http_client_server.cpp

Changes:
- Keep current relative resolution support.
- Preserve trailing slash semantics for path-relative redirects.
- Avoid extra temporary strings where possible in segment normalization:
  - parse/normalize in-place-style flow,
  - reserve based on input sizes to reduce reallocations.
- Keep query/fragment handling behavior explicit.

Tests:
- Extend resolveUrl tests for:
  - trailing slash preservation (dir/ -> .../dir/),
  - ./ and ../ with slash-ending paths,
  - query-only and fragment-only updates unchanged.

Acceptance:
- Relative redirect resolution is predictable and RFC-style for common cases.

---

### D) Global parser/body hard limits (hardened defaults)

Files:
- lib/include/HttpResponse.h
- lib/src/HttpResponse.cpp
- lib/src/HttpRequest.cpp
- tests/test_http_response_parser.cpp
- tests/test_http_request.cpp

Changes:
- Add explicit parser limits (defensive defaults):
  - request line max: keep 4 KiB (already present).
  - request header section max: 16 KiB total.
  - request body max accepted by parser: 16 MiB.
  - response header section max: 64 KiB.
  - response decoded body max: 64 MiB.
  - chunk size hard cap and checked arithmetic.
- Enforce limits incrementally (before unbounded append growth).
- Fail parser with Error/invalid when limits exceeded.

Notes on values:
- 16 KiB request headers: common reverse-proxy safe ceiling.
- 64 KiB response headers: allows larger cookie/header sets while bounded.
- 16 MiB request body parser cap: secure default for generic library parser.
- 64 MiB response cap: reasonable upper bound for client parser safety.

Tests:
- Oversized request headers rejected.
- Oversized request body by Content-Length rejected.
- Response parser rejects headers > cap.
- Response parser rejects decoded chunked body > cap.
- Chunk-size overflow/malformed values rejected cleanly.

Acceptance:
- Parser memory growth is bounded by explicit constants.

---

### E) Chunk parser arithmetic hardening

Files:
- lib/src/HttpResponse.cpp
- tests/test_http_response_parser.cpp

Changes:
- Replace soft pre-multiply guard with strict checked arithmetic.
- Enforce max chunk size and max decoded total body size.

Tests:
- Boundary tests at cap, cap+1, and overflow-formatted sizes.

Acceptance:
- No oversized chunk declaration can bypass cap checks.

---

## Non-Changes (Explicit)

- Detached DNS timeout thread in SocketImpl::resolveAddress_ remains as-is.
- Rationale: with blocking getaddrinfo and no portable cancellation, detached worker is an acceptable strategy to bound caller-observed timeout.
- Follow-up only if migrating to async resolver backend in future.

---

## Implementation Order

1. Malformed close semantics (A) + tests.
2. Request-target strictness (B) + tests.
3. Redirect resolution/path churn (C) + tests.
4. Global parser/body limits (D) + tests.
5. Chunk arithmetic hardening (E) + tests.
6. Run focused test binaries, then broader regression target if clean.
7. Commit and push.

---

## Verification Plan

Primary:
- Relevant unit tests:
  - tests/test_http_request.cpp
  - tests/test_http_response_parser.cpp
  - tests/test_http_poll_server.cpp
  - tests/test_http_client_server.cpp

Secondary:
- Existing HTTP/server integration tests already in repository.

Push criteria:
- All touched test suites pass.
- No new compiler/lint errors in touched files.

---

## Reminder Item

After implementation and push, report back with:
- What was merged.
- Any deviations from this plan.
- A reminder summary of the hardened profile limits adopted.
