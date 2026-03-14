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

| Metric | advanced_file_server (8080) | nginx (8082) | Winner |
|---|---|---|---|
| Req/sec | 11,332 | 19,511 | nginx (+72%) |
| Avg latency | 86.4 ms | 50.9 ms | nginx |
| Max latency | 136.9 ms | 153.4 ms | **ours** |
| Connect errors | 0 | 0 | tie |
| Read errors | 0 | 0 | tie |

---

## 10,000 Connections

```bash
# nginx
wrk -t12 -c10000 -d20s -H "Connection: keep-alive" http://localhost:8082/public.html

# advanced_file_server
wrk -t12 -c10000 -d20s -H "Connection: keep-alive" http://localhost:8080/public.html
```

| Metric | advanced_file_server (8080) | nginx (8082) | Winner |
|---|---|---|---|
| Req/sec | **8,075** | 3,836 | **ours (+110%)** |
| Avg latency | 428 ms | 364 ms | nginx |
| Max latency | 1.32 s | 1.84 s | **ours** |
| Connect errors | 0 | 0 | tie |
| Read errors | 14,617 | 262,832 | **ours (-94%)** |
| Timeouts | 0 | 13 | **ours** |
