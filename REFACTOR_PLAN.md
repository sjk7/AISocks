# AISocks Refactor Plan

## Goal
Reduce maintenance complexity in core networking and HTTP/TLS paths without changing external behavior.

## Status: All Core Phases Completed
All planned refactoring phases (3, 4, 5, and 6) have been implemented and verified with a 100% test pass rate.

### Completed Refactors
- **Phase 3**: Decomposed `SocketImpl.cpp` into focused units (`_Connect`, `_IO`, `_Options`).
- **Phase 4**: Introduced data-driven `TlsPolicy` in `TlsOpenSsl`.
- **Phase 5**: Simplified `HttpClientState` ownership and payload management.
- **Phase 6**: Extracted orchestration and logging from `ServerBase.h` into `ServerOrchestrator`.
- **Performance Audit**: Verified `ServerOrchestrator` handles 10k concurrent connections with ~6,700 requests/sec and zero stability issues.

## Next Steps / Future Work
- **Portability Testing**: Verify the recent `SocketImpl` decompositions on Windows (WSA) and Linux (epoll) via CI.
- **Deeper Policy Extraction**: Consider moving harder "Accept Filter" logic into a policy object if it grows more complex.

## Test Strategy
For any future changes:
1. Run targeted tests first (fast feedback).
2. Run broad suite (`ctest -j 8`) before merge.
3. Compare behavior against established baseline benchmarks.
