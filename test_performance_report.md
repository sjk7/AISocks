# AISocks Test Performance Summary (2026-03-21)

Total Suite Execution Time: **3.14 seconds**

## Top 10 Slowest Individual Test Cases

| Rank | Test Case Name | Duration (ms) |
| :--- | :--- | :--- |
| 1 | `--- test_tls_mtls_accept_reject ---` | 573.6 |
| 2 | `--- test_partial_io_and_eintr ---` | 548.6 |
| 3 | `--- HttpClient HTTPS keep-alive reuses TLS session across requests ---` | 415.2 |
| 4 | `--- test_tls_server_peer_subject ---` | 386.8 |
| 5 | `--- HttpClient HTTPS verify enabled fails for untrusted self-signed cert ---` | 324.4 |
| 6 | `--- DNS gate: queued calls consume timeout while waiting ---` | 232.6 |
| 7 | `--- test_tls_graceful_shutdown ---` | 232.0 |
| 8 | `--- test_tls_sni_and_rotation ---` | 228.7 |
| 9 | `--- Hook: onResponseSent called after complete response ---` | 214.8 |
| 10 | `--- Edge case: new connection attempts when at client limit ---` | 50.7 |

## Slowest Test Executables

1. `test_tls_client`: 3 seconds
2. `test_http_poll_server`: 3 seconds
3. `test_server_base_edge_cases`: 1 second
4. `test_server_base`: 1 second
