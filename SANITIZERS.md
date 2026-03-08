# Sanitizer Builds

This guide covers MemorySanitizer (MSan), AddressSanitizer (ASan), and
UndefinedBehaviorSanitizer (UBSan) builds for aiSocks.

---

## MemorySanitizer (MSan) via Docker

MSan requires every library — including the C++ standard library — to be
instrumented. On macOS this is done via a Docker image that uses Clang 14 +
`libc++` on Linux arm64.

### Prerequisites

- Docker installed and running
- The `aisocks-msan-arm64` image built (see below)

### 1. Build the Docker image

```bash
# VS Code task: "Docker: Build msan-arm64 image"
docker build --platform linux/arm64 -t aisocks-msan-arm64 -f Dockerfile.msan-arm64 .
```

The Dockerfile configures and builds the entire project with MSan inside the
image using the `msan-debug` CMake preset and Clang 14 + `libc++`.

### 2. Extract MSan binaries to `build-msan/`

```bash
# VS Code task: "Docker: Extract msan binaries"
docker run --rm --platform linux/arm64 \
  -v "${PWD}/build-msan:/out" \
  aisocks-msan-arm64 \
  cp -r /workspace/build-msan/. /out/
```

### 3. Debug in VS Code

Two debug configurations are available (Run and Debug panel):

| Configuration | Target binary |
|---|---|
| `Docker arm64 MSan: test_http_poll_server` | `build-msan/tests/test_http_poll_server` |
| `Docker arm64 MSan: advanced_file_server` | `build-msan/advanced_file_server` |

Each configuration automatically:
1. Runs **"Docker: Extract msan binaries"** to ensure binaries are current
2. Starts `lldb-server` inside the container on port 1234
3. Attaches VS Code LLDB for remote debugging
4. Tears down the container afterwards via **"Docker: Kill debug container"**

### MSan runtime options

The container runs with `MSAN_OPTIONS=halt_on_error=0` to avoid false positives
from uninstrumented libc stdout buffers. Change to `halt_on_error=1` in
`.vscode/tasks.json` to stop on the first real report.

---

## AddressSanitizer (ASan) — local build

ASan works natively on macOS with the system Clang.

### Configure and build

```bash
./scripts/setup_asan.sh          # creates build-asan/ with -DENABLE_ASAN=ON -DBUILD_EXAMPLES=ON
cmake --build build-asan         # or add e.g. --target advanced_file_server
```

### Run

```bash
ASAN_OPTIONS=halt_on_error=1:detect_stack_use_after_return=1:detect_leaks=1 \
  ./build-asan/advanced_file_server
```

---

## UndefinedBehaviorSanitizer (UBSan) — local build

```bash
./scripts/setup_ubsan.sh         # creates build-ubsan/ with -DENABLE_UBSAN=ON (RelWithDebInfo)
cmake --build build-ubsan
```

```bash
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ./build-ubsan/advanced_file_server
```

---

## VS Code tasks

All tasks are available via `Ctrl+Shift+P` → **Tasks: Run Task**:

| Task label | What it does |
|---|---|
| `Build test_http_poll_server` | Native debug build of the poll-server test |
| `Build advanced_file_server` | Native debug build of the file server |
| `Docker: Build msan-arm64 image` | Builds the MSan Docker image |
| `Docker: Extract msan binaries` | Copies MSan binaries out of the image into `build-msan/` |
| `Docker: Start lldb-server for test_http_poll_server` | Starts a remote debug session for the poll-server |
| `Docker: Start lldb-server for advanced_file_server` | Starts a remote debug session for the file server |
| `Docker: Kill debug container` | Removes the running `aisocks-debug` container |

---

## Performance impact

| Sanitizer | Slowdown | Memory overhead |
|---|---|---|
| MSan | 2–3× | High |
| ASan | 1.5–2× | Moderate |
| UBSan | ~1.1× | Minimal |

Use sanitizer builds for debugging only, not performance testing.

---

## Troubleshooting

**MSan "undefined symbol" errors** — a dependency was linked without MSan
instrumentation. Rebuild it inside the Docker image or suppress it via
`msan_ignorelist.txt`.

**ASan / UBSan: no output** — the binary ran cleanly. Verify `ASAN_OPTIONS` /
`UBSAN_OPTIONS` are set correctly if you expected a report.

**Build failures** — ensure Clang is the active compiler (`CC=clang CXX=clang++`).
Sanitizers are not supported by MSVC.
