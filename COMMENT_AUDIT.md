# Comment Audit — Library Files

Audit of `lib/include/` and `lib/src/` for inaccurate or not-useful comments.
Items are grouped by type, then ordered by impact.

---

## 1. Inaccurate comments

### 1.1 `lib/include/Socket.h` — blocking mode comment contradicts the implementation

**Location:** The `setBlocking()` block comment (lines ~99–104)

```cpp
// Blocking mode.
// Sockets are non-blocking by default: every freshly created socket
// (via SocketFactory or direct construction) has O_NONBLOCK / FIONBIO
// set immediately after the underlying ::socket() call.  Sockets
// returned by accept() inherit the listening socket's blocking mode.
// Call setBlocking(true) to opt into blocking semantics where needed.
```

**Why inaccurate:**  
`SocketImpl`'s constructor initialises `blockingMode = true` and never calls
`setBlocking(false)`. `SocketFactory` also contains no `setBlocking(false)`
call after creation. The `ServerBase` constructor contradicts this directly:

```cpp
// CRITICAL: Server listening socket must be non-blocking so the
// poller can check stop flags and handle timeouts properly.
// Sockets default to blocking mode, so we must explicitly set
// non-blocking for the server listener.
if (!listener_->setBlocking(false))
```

Sockets are **blocking by default**. The Socket.h comment should say the
opposite: call `setBlocking(false)` to opt into non-blocking semantics.

**Proposed fix:** Remove the inaccurate description and replace with:
```cpp
// Blocking mode.
// Newly created sockets are blocking (OS default).
// Call setBlocking(false) to switch to non-blocking mode.
// Sockets returned by accept() inherit the listening socket's blocking mode.
```

---

### 1.2 `lib/src/SocketImplHelpers.h` — `(void)errMsg` claims `errMsg` is unused when it is used

**Location:** `setSocketOption` template, first line of the function body

```cpp
(void)errMsg; // Suppress unused parameter warning
```

**Why inaccurate:**  
`errMsg` is used two lines later:

```cpp
fprintf(stderr, "setsockopt FAILED: %s\n", errMsg);
```

The `(void)` cast was probably  left over from an earlier version where the
`fprintf` block did not exist. It suppresses no warning here and is actively
misleading.

**Proposed fix:** Remove the `(void)errMsg;` line entirely.

---

### 1.3 `lib/src/PollerEpoll.cpp` — refers to non-existent `isValid()` method on `Poller`

**Location:** `Poller::Poller()` constructor, error-handling comment

```cpp
if (pImpl_->epfd == -1) {
    // Don't throw - set to invalid state
    // Users can check isValid() via the Poller methods
    pImpl_->epfd = -1;
}
```

**Why inaccurate:**  
`Poller` does not have an `isValid()` method (confirmed by `lib/include/Poller.h`).
Callers cannot check validity this way.

Additionally, the `pImpl_->epfd = -1` assignment is redundant: the `if` condition
`pImpl_->epfd == -1` already guarantees that value.

**Proposed fix:** Remove the inaccurate `isValid()` reference; remove the
redundant reassignment:
```cpp
if (pImpl_->kq == -1) {
    // kqueue() failed; wait() will return empty results.
}
```

---

### 1.4 `lib/src/PollerKqueue.cpp` — same redundant no-op reassignment

**Location:** `Poller::Poller()` constructor

```cpp
if (pImpl_->kq == -1) {
    // Don't throw - set to invalid state
    pImpl_->kq = -1;
}
```

**Why inaccurate / not useful:**  
Same issue as 1.3 — `pImpl_->kq = -1` inside `if (pImpl_->kq == -1)` is a
no-op.

**Proposed fix:** Remove the redundant reassignment (and simplify or remove comment).

---

## 2. Redundant / not-useful comments

### 2.1 `lib/src/SocketImplHelpers.h` — commented-out `assert` is dead code

**Location:** Near the end of `setSocketOption`

```cpp
// In debug mode, we want to know about this failure
// but we shouldn't crash - just return false
// assert(false
//   && "setsockopt failed - see stderr for detailed error//
//   information");
```

**Why not useful:**  
Commented-out code that is never compiled and provides no information a reader
cannot infer from the `fprintf` calls above it.

**Proposed fix:** Remove all four lines.

---

### 2.2 `lib/include/TcpSocket.h` — `sendChunked` inline comments restate the code

**Location:** `sendChunked` method body

```cpp
constexpr size_t chunkSize = 64 * 1024; // 64 KB chunks

// Send in chunks to handle partial sends gracefully
```

**Why not useful:**  
- `// 64 KB chunks` just restates the arithmetic.  
- `// Send in chunks...` is obvious from the function name `sendChunked`.

**Proposed fix:** Remove both comments.

---

### 2.3 `lib/src/SocketFactory.cpp` — `isPortAvailable` body comments state the obvious

**Location:** `SocketFactory::isPortAvailable`

```cpp
// Try to create a TCP socket and bind to the port
...
// Try to bind
...
// Successfully bound - port was available, but now it's occupied
// The socket will be closed when it goes out of scope
...
// Failed to bind - check if it's because port is in use
...
// Some other error occurred
```

**Why not useful:**  
Every comment restates what the immediately following code already says clearly.
The method name, variable names, and `if`/`else` structure are self-documenting.

**Proposed fix:** Remove all five comment blocks.

---

### 2.4 `lib/src/SocketImpl.cpp` — Unix `platformCleanup` comment is obvious

**Location:** `SocketImpl::platformCleanup()` (non-Windows branch)

```cpp
void SocketImpl::platformCleanup() {
    // Unix systems don't need cleanup
}
```

**Why not useful:**  
The empty function body already conveys this. The comment adds no information.

**Proposed fix:** Remove the comment.

---

### 2.5 `lib/src/SocketImpl.cpp` — constructor boot comment states what the initialiser list already shows

**Location:** `SocketImpl` main constructor, just before `#ifdef SO_NOSIGPIPE`

```cpp
// Sockets default to blocking mode (OS standard behavior).
// Call setBlocking(false) explicitly if non-blocking is needed.
// blockingMode is initialized to true in the member-initializer list.
```

**Why not useful:**  
The first two lines are correct but duplicate the authoritative API-level
documentation that should live in `Socket.h` (once item 1.1 is fixed).
The third line literally describes the initialiser `blockingMode(true)` visible
two lines earlier — pure noise.

**Proposed fix:** Remove all three lines.

---

### 2.6 `lib/src/TcpSocket.cpp` — sendfile comments state the obvious

**Location:** `TcpSocket::sendfile`

```cpp
// Use OS sendfile for zero-copy transfer
```
and
```cpp
// macOS sendfile takes different parameters
```

**Why not useful:**  
- The first comment states what `sendfile` literally is.  
- The second comment says the code uses different parameters, but the different
  parameters are immediately self-evident from the `#elif __APPLE__` branch.

**Proposed fix:** Remove both lines.

---

### 2.7 `lib/src/SocketImplHelpers.h` — diagnostic print comments in `setSocketOption` are not useful

**Location:** `setSocketOption` template, before the `fprintf` block

```cpp
// Get the actual error instead of just asserting

// Print detailed error information for debugging
```

**Why not useful:**  
- "Get the actual error instead of just asserting" — the "instead of" implies
  there was a previous assert-based version; that history is irrelevant to
  future readers.
- "Print detailed error information for debugging" — obvious from the
  `fprintf(stderr, ...)` calls that follow.

**Proposed fix:** Remove both lines.

---

## 3. Summary table

| # | File | Type | Description |
|---|------|------|-------------|
| 1.1 | `lib/include/Socket.h` | Inaccurate | Claims sockets default to non-blocking; they are blocking by default |
| 1.2 | `lib/src/SocketImplHelpers.h` | Inaccurate | `(void)errMsg` claims the parameter is unused but it is used |
| 1.3 | `lib/src/PollerEpoll.cpp` | Inaccurate + redundant | References non-existent `isValid()` method; redundant `= -1` assignment |
| 1.4 | `lib/src/PollerKqueue.cpp` | Redundant | `pImpl_->kq = -1` inside `if (kq == -1)` is a no-op |
| 2.1 | `lib/src/SocketImplHelpers.h` | Dead code | Commented-out `assert` block |
| 2.2 | `lib/include/TcpSocket.h` | Redundant | `sendChunked` comments restate the code/name |
| 2.3 | `lib/src/SocketFactory.cpp` | Redundant | `isPortAvailable` comments describe self-evident code |
| 2.4 | `lib/src/SocketImpl.cpp` | Redundant | `platformCleanup` has obvious in-body comment |
| 2.5 | `lib/src/SocketImpl.cpp` | Redundant | Constructor blocking-mode comment restates what is visible |
| 2.6 | `lib/src/TcpSocket.cpp` | Redundant | `sendfile` comments state what the code already expresses |
| 2.7 | `lib/src/SocketImplHelpers.h` | Redundant | Diagnostic-print lead-in comments are obvious |
