# Performance Benchmarks

## Test Environment

- **Machine**: Apple Silicon (arm64), mains power
- **Tool**: [wrk](https://github.com/wg/wrk)
- **Opponent**: nginx (worker_processes=auto)
- **Build**: RelWithDebInfo
- **Servers run sequentially** (nginx first, then ours)

---

## 1,000 Connections

```bash
# nginx
wrk -t12 -c1000 -d20s -H "Connection: keep-alive" http://localhost:8082/public.html

# advanced_file_server
wrk -t12 -c1000 -d20s -H "Connection: keep-alive" http://localhost:8080/public.html
```

| Metric | advanced_file_server (8080) | nginx (8082) |
|---|---|---|
| Req/sec | 11,332 | 19,511 |
| Avg latency | 86.4 ms | 50.9 ms |
| Max latency | 136.9 ms | 153.4 ms |
| Connect errors | 0 | 0 |
| Read errors | 0 | 0 |

---

## 10,000 Connections

```bash
# nginx
wrk -t12 -c10000 -d20s -H "Connection: keep-alive" http://localhost:8082/public.html

# advanced_file_server
wrk -t12 -c10000 -d20s -H "Connection: keep-alive" http://localhost:8080/public.html
```

| Metric | advanced_file_server (8080) | nginx (8082) |
|---|---|---|
| Req/sec | **8,075** | 3,836 |
| Avg latency | 428 ms | 364 ms |
| Max latency | 1.32 s | 1.84 s |
| Connect errors | 0 | 0 |
| Read errors | 14,617 | 262,832 |
| Timeouts | 0 | 13 |
