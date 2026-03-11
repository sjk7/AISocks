# Bug Fix Plan

Identified during code review on 11 March 2026.
Work through these in order — later items depend on earlier infrastructure fixes.

---

## 1. `atoll` on non-null-terminated `string_view` (correctness/security)

**File:** `lib/src/HttpResponse.cpp` — `determineBodyMode_()`
**Line:** `contentLength_ = static_cast<int64_t>(std::atoll(cl->data()));`

`cl` is a `string_view` into `headerBuf_`. `atoll` reads until a non-digit and is not length-bounded. Replace with `std::from_chars` which is bounds-safe, locale-free, and faster.

```cpp
// Replace:
contentLength_ = static_cast<int64_t>(std::atoll(cl->data()));

// With:
int64_t parsed = 0;
auto [ptr, ec] = std::from_chars(cl->data(), cl->data() + cl->size(), parsed);
if (ec == std::errc{}) contentLength_ = parsed;
else { markError_(); return; }
```

---

## 2. Chunked trailer parser declares complete too early (correctness/RFC)

**File:** `lib/src/HttpResponse.cpp` — `processChunked_()`

When the terminal `0\r\n` chunk is found, the code searches for the *first* `\r\n` after it and immediately calls `markComplete_()`. This fires on the opening `\r\n` of a trailer header instead of the final `\r\n\r\n` that closes the entire trailer section. Result: body is marked complete before the real end of stream when trailers are present.

**Fix:** Search for `\r\n\r\n` (the end of the trailer/close section) instead of `\r\n`.

```cpp
// Replace:
const size_t trailerEnd = bodyBuf_.find("\r\n", crlfPos + 2);
if (trailerEnd == std::string::npos) break; // need more data

// With:
const size_t trailerEnd = bodyBuf_.find("\r\n\r\n", crlfPos + 2);
if (trailerEnd == std::string::npos) break; // need more data
```

---

## 3. `resolveKeepAlive_` is case-sensitive on `Connection` header value (RFC non-compliance)

**File:** `lib/src/HttpPollServer.cpp` — `resolveKeepAlive_()`

Header *keys* are lowercased on parse, but *values* are not. `Connection: Keep-Alive` stores value `"Keep-Alive"`, which does not match `"keep-alive"`. RFC 7230 requires case-insensitive token comparison. Multi-token values (`Connection: close, keep-alive`) also silently break.

**Fix:** Lowercase the value before comparing, and compare via `string_view` case-folding helper or a lowercase copy.

```cpp
// Replace:
const bool hasKeepAlive = conn && (*conn == "keep-alive");
const bool hasClose     = conn && (*conn == "close");

// With:
auto ciEqual = [](std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (::tolower((unsigned char)a[i]) != (unsigned char)b[i]) return false;
    return true;
};
const bool hasKeepAlive = conn && ciEqual(*conn, "keep-alive");
const bool hasClose     = conn && ciEqual(*conn, "close");
```

Also handle comma-separated token lists in the Connection header per RFC (e.g. `Connection: TE, keep-alive`).

---

## 4. Slowloris timeout constant vs. comment mismatch (reliability)

**File:** `lib/include/HttpPollServer.h` and `lib/src/HttpPollServer.cpp`

`SLOWLORIS_TIMEOUT_MS = 200` — 200 ms is far too aggressive. Slow or high-latency connections (mobile, satellite, congested links) legitimately take longer to send headers.
The inline comment in `HttpPollServer.cpp` says "5 seconds" — contradicting the constant.

**Fix:** Raise the constant to a realistic value (5000 ms is common) and fix the comment.

```cpp
// HttpPollServer.h:
static constexpr int SLOWLORIS_TIMEOUT_MS = 5000;  // 5 s

// HttpPollServer.cpp comment:
// Slowloris protection: drop if headers not received within 5 seconds.
```

---

## 5. `string_view reason_` lifetime trap in `HttpResponse::Builder` (undefined behavior)

**File:** `lib/include/HttpResponse.h` — `Builder` class

`reason_` is a `string_view`. If a caller passes a temporary `std::string` to `.status()`, the view danges immediately after the call.

```cpp
// Dangerous but compiles silently:
builder.status(200, std::string("computed reason"));
// reason_ now points to freed memory.
```

**Fix:** Change `std::string_view reason_` to `std::string reason_` in `Builder`.

```cpp
// Replace in Builder private section:
std::string_view reason_;
// With:
std::string reason_;
```

Also update `getReason_` to return `std::string_view` from a `const std::string&` (already compatible).

---

## 6. `urlDecode` / `urlDecodePath` code duplication

**File:** `lib/src/UrlCodec.cpp`

The two functions are identical except for `+` → space treatment. Duplication means any future bug fix must be applied twice.

**Fix:** Extract a shared implementation:

```cpp
static std::string urlDecodeImpl(std::string_view src, bool plusIsSpace) {
    std::string out;
    out.reserve(src.size());
    for (size_t i = 0, n = src.size(); i < n; ++i) {
        const unsigned char c = static_cast<unsigned char>(src[i]);
        if (c == '%' && i + 2 < n) {
            const uint8_t hi = kFromHex[static_cast<unsigned char>(src[i + 1])];
            const uint8_t lo = kFromHex[static_cast<unsigned char>(src[i + 2])];
            if (hi != 0xFF && lo != 0xFF) {
                out += static_cast<char>((hi << 4) | lo);
                i += 2;
                continue;
            }
        } else if (plusIsSpace && c == '+') {
            out += ' ';
            continue;
        }
        out += static_cast<char>(c);
    }
    return out;
}

std::string urlDecode(std::string_view src)     { return urlDecodeImpl(src, true);  }
std::string urlDecodePath(std::string_view src) { return urlDecodeImpl(src, false); }
```

---

## 7. Header-name lowercasing duplicated with mismatched thresholds

**Files:** `lib/src/HttpRequest.cpp` (`header()`), `lib/src/HttpResponse.cpp` (`header()`)

Both implement the same manual `tolower` loop into a stack buffer but with different sizes (64 vs 128) and slightly different fallback logic.

**Fix:** Extract to a shared helper in `HttpParserUtils.h`:

```cpp
// HttpParserUtils.h (add alongside existing helpers):
template <typename Map>
const std::string_view* lookupHeaderCI(const Map& headers, std::string_view name) {
    char sbuf[128];
    std::string heap;
    const char* data;
    if (name.size() < sizeof(sbuf)) {
        for (size_t i = 0; i < name.size(); ++i)
            sbuf[i] = static_cast<char>(::tolower(static_cast<unsigned char>(name[i])));
        data = sbuf;
    } else {
        heap.resize(name.size());
        for (size_t i = 0; i < name.size(); ++i)
            heap[i] = static_cast<char>(::tolower(static_cast<unsigned char>(name[i])));
        data = heap.c_str();
    }
    auto it = headers.find(std::string_view(data, name.size()));
    return it == headers.end() ? nullptr : &it->second;
}
```

Then both `HttpRequest::header()` and `HttpResponse::header()` delegate to this.

---

## 8. `Builder::build()` Content-Length check is case-sensitive (correctness)

**File:** `lib/src/HttpResponse.cpp` — `Builder::build()`

```cpp
if (h.first == "Content-Length") hasContentLength = true;
```

A caller passing `"content-length"` bypasses this and produces a duplicate header.

**Fix:** Use case-insensitive comparison (or normalise keys to canonical case when `header()` is called):

```cpp
// Simple fix: compare lowercased
auto lowerEqual = [](const std::string& a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (::tolower((unsigned char)a[i]) != (unsigned char)b[i]) return false;
    return true;
};
if (lowerEqual(h.first, "content-length")) hasContentLength = true;
```

---

## 9. `FileCache`: 3 hash lookups per hit (performance)

**File:** `lib/include/FileCache.h`, `lib/src/FileCache.cpp`

The `lruIndex_` is a second parallel `unordered_map` used only to map keys to `list` iterators. Every cache hit does:
1. `cache_.find(path)` — find cache entry
2. `lruIndex_.find(path)` — find LRU iterator (in `get()`)
3. `lruIndex_.find(path)` again (inside `updateLRU`)

**Fix:** Store the `list<string>::iterator` directly inside `CachedFile`:

```cpp
struct CachedFile {
    std::vector<char> content;
    time_t lastModified = 0;
    size_t size = 0;
    std::list<std::string>::iterator lruIt; // NEW: eliminates lruIndex_
};
```

Remove `lruIndex_` from the class. All LRU operations on hit become a single list splice via `it->second.lruIt` — one lookup total.

---

## 10. Chunked body double-buffers full response in memory (memory efficiency)

**File:** `lib/src/HttpResponse.cpp` — `processChunked_()`

`bodyBuf_` accumulates all raw encoded bytes. As chunks are consumed (up to `chunkScanPos_`), the prefix is dead weight. For a large chunked response, peak RSS = encoded + decoded sizes simultaneously.

**Fix:** Periodically compact `bodyBuf_` after consuming chunks:

```cpp
// At the end of processChunked_(), after the while loop:
if (chunkScanPos_ > 4096 && chunkScanPos_ > bodyBuf_.size() / 2) {
    bodyBuf_.erase(0, chunkScanPos_);
    chunkScanPos_ = 0;
}
```

The threshold avoids O(n²) for many tiny chunks but recovers memory after large ones.

---

## 11. `parseHeaderFields` allocates heap string for every header key (performance)

**File:** `lib/src/HttpParserUtils.h` — `parseHeaderFields()`

```cpp
std::string key;
key.resize(rawKey.size());
```

Headers like `transfer-encoding` (17 chars) exceed libstdc++ SSO (~15 chars), allocating on the heap for every header. If `emit` stores the key by value anyway, this is unavoidable — but the construction can be done in one step with `std::string(rawKey)` + an in-loop `tolower`, rather than a resize + fill.

**Fix:** Use `transform` to construct and lowercase in one pass, or (better) let the compiler apply SSO more aggressively with a fixed transform:

```cpp
std::string key(rawKey.size(), '\0');
std::transform(rawKey.begin(), rawKey.end(), key.begin(),
    [](unsigned char c){ return static_cast<char>(::tolower(c)); });
emit(std::move(key), rawVal);
```

(This is a clean-up/micro-opt; it doesn't change the allocation pattern for long keys but is more idiomatic.)

---

---

## Footguns — API Safety Hazards

These are **silent undefined behaviour traps** that affect any caller of the public API.
They produce no compiler warning, compile cleanly, and explode at runtime in ways that
are very hard to debug. Fix these before the API reaches more consumers.

---

### FG-1. `HttpRequest` string_view fields alias the *caller's* buffer (dangling views)

**File:** `lib/include/HttpRequest.h`

`HttpRequest` holds these fields as `string_view`:

```cpp
std::string_view method;
std::string_view rawPath;
std::string_view queryString;
std::string_view version;
std::string_view body;
```

All of them point directly into the raw buffer passed to `HttpRequest::parse(raw)`.
The moment the caller's buffer is freed, moved-from, or goes out of scope, every one
of these views becomes a dangling pointer. The struct gives **no clear ownership signal**
about this requirement. This is the single most dangerous footgun in the library.

**Typical crash scenario:**
```cpp
HttpRequest req;
{
    std::string raw = readFromSocket();
    req = HttpRequest::parse(raw);
} // raw destroyed here — ALL string_view fields are now dangling
processRequest(req); // UB: reads freed memory
```

**Fix options (pick one):**
- **Option A (safest) — own the buffer:** Add `std::string ownedBuf` to `HttpRequest`; copy `raw` into it in `parse()` and make all views point into `ownedBuf`. Zero API breakage; ~1 extra allocation per parse.
- **Option B — make the hazard explicit:** Rename to `HttpRequestView` and add a non-view sibling `HttpRequest` that owns its data. Users opt in to the lifetime contract deliberately.
- **Option C — static analysis enforcement:** At minimum, add `[[nodiscard]]` to `parse()` and a prominent "LIFETIME" block in the header doc (already partially there but not prominent enough). Add a `std::string ownedBuf` and a factory `HttpRequest::parseOwned(std::string)` that stores the buffer inside the struct.

**Recommended:** Option A for a library. Option C is the minimum acceptable near-term mitigation.

---

### FG-2. `HttpResponse` string_view fields alias the *parser's* internal buffers

**File:** `lib/include/HttpResponse.h`

Every `string_view` returned from an `HttpResponse` (`version()`, `statusText()`,
`body()`, `header(name)`, and all values in `headers()`) points into buffers owned by
the `HttpResponseParser` that produced the response.

**Crash scenarios:**
```cpp
// Scenario 1: parser destroyed before response fields are used
std::string_view body;
{
    HttpResponseParser p;
    p.feed(data, len);
    body = p.response().body(); // body_ points into p.bodyBuf_ or p.decodedBody_
} // p destroyed — body is dangling
printf("%.*s", (int)body.size(), body.data()); // UB

// Scenario 2: reset() called without consuming the response
parser.feed(data, len); // complete
auto& resp = parser.response();
parser.reset();         // clears ALL internal buffers
resp.statusCode;        // int — fine
resp.body();            // string_view — dangling
```

The doc comment says "Do not use them after the parser is destroyed or reset()" — but this
is a very easy mistake and there is no enforcement mechanism.

**Fix:**
- Give `HttpResponse` an `extract()` method (or a move constructor) that internalises
  all the buffers from the parser into the response object itself, converting views to
  owned `std::string` members. The parser calls this before `reset()`, and the caller
  uses `extract()` to get a fully self-contained response:

```cpp
// Proposed API:
HttpResponse resp = parser.extractResponse(); // moves buffers in, converts views
parser.reset();
// resp is now self-contained; views are valid for resp's lifetime
```

Alternatively, provide a `HttpResponse::Owned` value type with `std::string` members
and a conversion from the parser-backed `HttpResponse`.

---

### FG-3. `Builder::status()` stores a `string_view` to a potentially temporary `std::string`

*(Already listed as Bug #5 — repeated here for completeness.)*

**File:** `lib/include/HttpResponse.h` — `Builder`

```cpp
Builder& status(int code, std::string_view reason = "") {
    reason_ = reason; // stores a view — will dangle if reason was a temporary
    ...
}
std::string_view reason_; // <- the trap
```

This is also a footgun because `.status(200, generateReason())` compiles with no warning
and silently stores a dangling view. Fix: change `reason_` to `std::string`.

---

### FG-4. `HttpClientState` copy/move has fragile `responseView` pointer fixup

**File:** `lib/include/HttpPollServer.h` — `HttpClientState` copy/move operators

```cpp
HttpClientState(HttpClientState&& other) noexcept {
    ...
    // After the move, fix up view if it was backed by the (now moved) buf.
    if (!responseBuf.empty()) responseView = responseBuf;
    other.responseView = {};
}
```

This fixup is only applied to the case where `responseView` pointed into `responseBuf`.
But `responseView` can also point into *server-owned* long-lived storage (the zero-copy
path). After a move, that pointer is still valid — but the fixup unconditionally
overwrites `responseView` with `responseBuf` whenever `responseBuf` is non-empty,
**even if `responseView` was not pointing into it**. This silently switches from
zero-copy to a buffered copy after any move of the state struct, breaking the invariant
that zero-copy responses remain zero-copy.

Additionally, the copy assignment operator has the same issue: it conditionally redirects
`responseView` only when `other.responseView.data() == other.responseBuf.data()`, which
is correct, but fails to handle the case where `responseBuf` was non-empty but
`responseView` pointed elsewhere (e.g., into a separately invalidated buffer).

**Fix:** Track the source of `responseView` explicitly with a boolean flag or enum:

```cpp
enum class ResponseSource { None, StaticStorage, DynamicBuf };
ResponseSource responseSource{ResponseSource::None};
```

Then all copy/move special members become unambiguous: if `responseSource == DynamicBuf`,
redirect `responseView`; otherwise leave it as-is.

---

### FG-5. `isHttpRequest()` matches on 4-char prefixes only (logical footgun)

**File:** `lib/src/HttpPollServer.cpp`

```cpp
bool HttpPollServer::isHttpRequest(const std::string& req) {
    return req.rfind("GET ", 0) == 0 || req.rfind("POST", 0) == 0
        || req.rfind("PUT ", 0) == 0 || req.rfind("HEAD", 0) == 0
        || req.rfind("DELE", 0) == 0 || req.rfind("OPTI", 0) == 0
        || req.rfind("PATC", 0) == 0;
}
```

`"POSTAL / HTTP/1.1"` matches `"POST"`. `"DELETE"` matches because `"DELE"` is a
prefix of it — but so does anything starting with those 4 chars. This function is
exposed as a public `static` helper but its contract is misleading. A caller relying on
it to validate method names gets a false positive.

**Fix:** Match complete method tokens followed by a space:

```cpp
static bool startsWithMethod(const std::string& req, const char* method) {
    const size_t mlen = strlen(method);
    return req.size() > mlen && req.compare(0, mlen, method) == 0 && req[mlen] == ' ';
}

bool HttpPollServer::isHttpRequest(const std::string& req) {
    return startsWithMethod(req, "GET")
        || startsWithMethod(req, "POST")
        || startsWithMethod(req, "PUT")
        || startsWithMethod(req, "HEAD")
        || startsWithMethod(req, "DELETE")
        || startsWithMethod(req, "OPTIONS")
        || startsWithMethod(req, "PATCH");
}
```

---

## Priority Order

| # | Severity | Est. Effort |
|---|----------|-------------|
| FG-1 | **Critical** — silent UB, any caller, hard to debug | Medium |
| FG-2 | **Critical** — silent UB, any parser consumer | Medium |
| FG-3 / Bug 5 | High — silent UB, dangling view | Trivial |
| FG-4 | High — silent behavioural regression on move | Small |
| 2 | High — correctness, RFC compliance | Small |
| 1 | High — correctness/security | Small |
| 3 | High — RFC non-compliance, interop | Small |
| FG-5 | Medium — misleading API contract | Small |
| 4 | Medium — reliability (real-world disconnects) | Trivial |
| 8 | Medium — correctness | Trivial |
| 6 | Low — maintainability | Small |
| 7 | Low — maintainability | Small |
| 10 | Low — memory efficiency | Small |
| 9 | Low — performance | Medium |
| 11 | Low — micro-perf / style | Trivial |

---

## Test files to run after each fix

- `tests/test_http_response_parser.cpp` — covers issues 1, 2, 3, 10
- `tests/test_http_request.cpp` — covers issues 3, 7
- `tests/test_url_codec.cpp` — covers issue 6
- `tests/test_file_cache.cpp` — covers issue 9
- `tests/test_http_poll_server.cpp` / `test_http_stress.cpp` — covers issues 3, 4
- `tests/fuzz_http_response.cpp` / `fuzz_http_request.cpp` — regression fuzz for 1, 2
