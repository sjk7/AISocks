// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
#ifndef AISOCKS_SERVER_BASE_H
#define AISOCKS_SERVER_BASE_H

#include "Poller.h"
#include "ServerSignal.h"
#include "ServerTypes.h"
#include "TcpSocket.h"
#include "SocketFactory.h"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
#ifndef _WIN32
#include <sys/resource.h>
#endif

namespace aiSocks {

// Number of client slots reserved at startup when no explicit limit is set.
// Used as the floor for clients_ and timeout_heap_ pre-allocation so that the
// first burst of connections never triggers a rehash or reallocation.
inline constexpr size_t defaultMaxClients
    = static_cast<size_t>(ClientLimit::Default);

// Return values for ServerBase virtual functions
enum class ServerResult {
    KeepConnection = 1, // Keep the connection alive
    Disconnect = 0, // Disconnect this client
    StopServer = -1 // Stop the server gracefully
};

// ---------------------------------------------------------------------------
// ServerBase<ClientData>  CRTP-free base for poll-driven TCP servers.
//
// Manages the listening socket, Poller registration, client socket lifetime,
// and per-client state storage.  Derived classes only need to implement the
// four virtual hooks:
//
//   ServerResult onReadable(TcpSocket& sock, ClientData& data)
//     Called when the client socket has incoming data.
//     Return ServerResult::Disconnect to disconnect and destroy the client.
//     Return ServerResult::StopServer to stop the server gracefully.
//     Return ServerResult::KeepConnection to continue.
//
//   ServerResult onWritable(TcpSocket& sock, ClientData& data)
//     Called when the client socket buffer has space for more data.
//     Return ServerResult::Disconnect to disconnect and destroy the client.
//     Return ServerResult::StopServer to stop the server gracefully.
//     Return ServerResult::KeepConnection to continue.
//
//   ServerResult onDisconnect(ClientData& data)   [optional]
//     Called just before a client entry is removed (error, or Disconnect
//     return from onReadable/onWritable).  Default returns Disconnect.
//
//   ServerResult onIdle()   [optional]
//     Called when poller.wait() times out with no ready events.
//     NOT called when events fire.  Default returns KeepConnection.
//
// Usage:
//   struct MyState { std::string inbuf; std::string outbuf; size_t sent{}; };
//
//   class MyServer : public ServerBase<MyState> {
//   public:
//       explicit MyServer(const ServerBind& b) : ServerBase(b) {}
//   protected:
//       ServerResult onReadable(TcpSocket& sock, MyState& s) override {
//           return ServerResult::KeepConnection;
//       }
//       ServerResult onWritable(TcpSocket& sock, MyState& s) override {
//           return ServerResult::KeepConnection;
//       }
//   };
//
//   MyServer srv(ServerBind{"0.0.0.0", Port{9000}});
//   if (!srv.isValid()) {
//       // Handle server creation failure
//       return;
//   }
//   srv.run();
//
//   // With detailed error information:
//   Result<TcpSocket> serverResult;
//   MyServer srv(ServerBind{"0.0.0.0", Port{9000}}, AddressFamily::IPv4,
//   &serverResult); if (!srv.isValid()) {
//       fprintf(stderr, "Server failed: %s\n", serverResult.message().c_str());
//       return;
//   }
//   srv.run();
//
// On failure (bind, listen, or Poller registration), the server is leftimeour
// invalid; check isValid(). run() blocks until there are no more clients and
// accepting has stopped.
// ---------------------------------------------------------------------------

template <typename ClientData> class ServerBase {
    public:
    // Construct and start listening.  Does not accept until run() is called.
    // Returns invalid server if bind or listen fails - check isValid().
    // The detailed error information is moved into the result parameter.
    explicit ServerBase(const ServerBind& args,
        AddressFamily family = AddressFamily::IPv4,
        Result<TcpSocket>* result = nullptr)
        : listener_(std::make_unique<TcpSocket>(TcpSocket::createRaw(family))) {
        // Pre-size the sparse fd→client table to the process fd ceiling
        // here in the constructor, before any threading or accept() calls.
        // run() can then insert and erase clients in O(1) without ever
        // resizing clientSlots_ in the hot path.
        clientSlots_.resize(getFdCeiling());
        // Use SocketFactory to create server without exceptions
        auto createResult = SocketFactory::createTcpServer(family, args);
        if (createResult.isSuccess()) {
            printf("DEBUG: SocketFactory::createTcpServer() succeeded\n");
            *listener_ = std::move(createResult.value());
            // CRITICAL: Server listening socket must be non-blocking so the
            // poller can check stop flags and handle timeouts properly.
            // Sockets default to blocking mode, so we must explicitly set
            // non-blocking for the server listener.
            if (!listener_->setBlocking(false))
                printf("Warning: Failed to set non-blocking mode on server "
                       "socket\n");
            // Set server-wide policies on the listener so accepted sockets
            // inherit them via propagateSocketProps.
            if (!listener_->setNoDelay(true))
                printf("Warning: Failed to set TCP_NODELAY on server socket\n");
            (void)listener_->setReceiveBufferSize(256 * 1024);
            (void)listener_->setSendBufferSize(256 * 1024);
        } else {
            // Server creation failed - move error info to result parameter
            if (result) {
                *result = std::move(createResult);
            }
            // Reset the listener to make the server invalid
            listener_.reset();
            // Still print the error for backward compatibility
            fprintf(stderr,
                "FATAL: SocketFactory::createTcpServer() failed with error "
                "code %d: %s\n",
                static_cast<int>(createResult.error()),
                createResult.message().c_str());
            fprintf(stderr,
                "FATAL: Cannot start server - port %d is already in use or "
                "invalid\n",
                args.port.value());
            // exit(1); // NO! Bad form!
        }
    }

    virtual ~ServerBase() {
        // Clean up any remaining clients before destruction.
        // Full clear here (unlike run() cleanup) since the object is gone.
        clientSlots_.clear();
        clientFds_.clear();
        current_poller_ = nullptr;
    }

    // Non-copyable, movable.
    ServerBase(const ServerBase&) = delete;
    ServerBase& operator=(const ServerBase&) = delete;
    ServerBase(ServerBase&&) = default;
    ServerBase& operator=(ServerBase&&) = default;

    // Check if the server is valid and ready for use
    bool isValid() const { return listener_ && listener_->isValid(); }

    // Enter the poll loop.
    //
    // maxClients: ClientLimit::Unlimited = unlimited; N > 0 = stop accepting
    // after N connections,
    //             but continue serving existing clients until all disconnect.
    // timeout:    passed to Poller::wait(); -1 = block until an event.
    //
    // Returns when there are no remaining connected clients (and accepting is
    // stopped, either because maxClients was reached or you stopped
    // externally).
    void run(ClientLimit maxClients = ClientLimit::Default,
        Milliseconds timeout = Milliseconds{-1}) {
        if (!isValid()) return; // Server not valid, exit early

        stop_.store(false, std::memory_order_relaxed);

        // Install SIGINT/SIGTERM for Ctrl+C shutdown; restore on exit.
        //
        // NOTE: signal_stop_ is a process-wide static flag shared by every
        // ServerBase instance.  If two servers run concurrently and both have
        // signal handling enabled, SIGINT will stop both.  Use
        // setHandleSignals(false) on instances that should be unaffected.
        using SigHandler = void (*)(int);
        SigHandler prevInt = SIG_DFL;
        SigHandler prevTerm = SIG_DFL;
        if (handleSignals_) {
            prevInt = std::signal(SIGINT, serverHandleSignal);
            prevTerm = std::signal(SIGTERM, serverHandleSignal);
        }
        struct SigGuard {
            bool active;
            SigHandler pi;
            SigHandler pt;
            ~SigGuard() {
                if (active) {
                    std::signal(SIGINT, pi);
                    std::signal(SIGTERM, pt);
                }
            }
        } guard{handleSignals_, prevInt, prevTerm};

        Poller poller;
        current_poller_ = &poller;
        if (!poller.add(*listener_, PollEvent::Readable | PollEvent::Error)) {
            return; // Failed to register with poller
        }

        // Always pre-reserve both containers so that the first burst of
        // connections never triggers a rehash or vector reallocation.
        // When Unlimited is requested we still use defaultMaxClients as a
        // sensible starting capacity; the containers will grow automatically
        // if more clients arrive.
        {
            const size_t cap = (maxClients == ClientLimit::Unlimited)
                ? defaultMaxClients
                : static_cast<size_t>(maxClients);
            // Pre-size the sparse fd→client array to the process fd ceiling
            // once so that emplaceClient() never resizes in the hot path.
            // The OS cannot issue an fd at or beyond its own limit, so this
            // single upfront allocation is exact — no gap, no over-run.
            if (clientSlots_.empty()) clientSlots_.resize(getFdCeiling());
            clientFds_.reserve(cap);
            // Each client can have multiple heap entries while stale ones
            // accumulate between sweeps, so reserve 2x the client capacity.
            timeout_heap_.reserve(cap * 2);
        }

        bool accepting = true;
        size_t accepted = 0;

        while (!stop_.load(std::memory_order_relaxed)
            && !(handleSignals_
                && g_serverSignalStop.load(std::memory_order_relaxed))
            && (accepting || !clientFds_.empty())) {
            auto ready = poller.wait(timeout);
            if (stop_.load(std::memory_order_relaxed)
                || (handleSignals_
                    && g_serverSignalStop.load(std::memory_order_relaxed)))
                break;
            for (const auto& event : ready) {
                if (event.socket == listener_.get()) {
                    if (!accepting) continue;
                    drainAccept(poller, accepting, accepted, maxClients);
                    continue;
                }

                uintptr_t cfd = event.socket->getNativeHandle();
                ClientEntry* ce = findClient(cfd);
                if (!ce) continue;

                bool keep = !hasFlag(event.events, PollEvent::Error);
                if (!keep) {
                    ServerResult errResult = onError(*ce->socket, ce->data);
                    if (errResult == ServerResult::StopServer) {
                        stop_.store(true);
                        break;
                    }
                    keep = (errResult == ServerResult::KeepConnection);
                }
                if (keep && hasFlag(event.events, PollEvent::Readable)) {
                    ServerResult result = onReadable(*ce->socket, ce->data);
                    if (result == ServerResult::StopServer) {
                        stop_.store(true);
                        break;
                    }
                    keep = (result == ServerResult::KeepConnection);
                }
                if (keep && hasFlag(event.events, PollEvent::Writable)) {
                    ServerResult result = onWritable(*ce->socket, ce->data);
                    if (result == ServerResult::StopServer) {
                        stop_.store(true);
                        break;
                    }
                    keep = (result == ServerResult::KeepConnection);
                }

                if (!keep) {
                    onDisconnect(ce->data);
                    ce->socket->shutdown(ShutdownHow::Both);
                    (void)poller.remove(*ce->socket);
                    eraseClient(cfd);
#ifdef SERVER_STATS
                    printf("[stats] clients: %zu  peak: %zu\n",
                        clientFds_.size(), peak_clients_);
#endif
                }
            }

            // Drain expired keep-alive entries.  Under heavy load (>1000
            // clients) throttle to at most one sweep per 100 ms: the
            // fast-path SteadyClock::now() is cheap but not free, and the
            // event loop iterates far faster than timeouts actually fire.
            {
                const auto sweepNow = SteadyClock::now();
                if (clientFds_.size() < 1000
                    || (sweepNow - lastSweepTime_)
                        >= std::chrono::milliseconds{100}) {
                    sweepTimeouts(poller);
                    lastSweepTime_ = sweepNow;
                }
            }

            // onIdle() is called only when poller.wait() returned with no
            // ready events, i.e. a genuine timeout.  Callers that want
            // reliable periodic semantics should pass a bounded timeout to
            // run() (e.g. Milliseconds{100}) rather than guarding with their
            // own wall-clock timer.
            if (ready.empty()) {
                if (onIdle() == ServerResult::StopServer) {
                    stop_.store(true);
                    break;
                }
            }
        }

        printf(
            "DEBUG: Event loop exited - stop=%d, accepting=%d, clients=%zu\n",
            stop_.load(std::memory_order_relaxed), accepting,
            clientFds_.size());

        // Flush any accumulated keep-alive timeout count before exiting.
        if (timeoutLogCount_ > 0) {
            printf("[keepalive] closed %zu idle connection%s total\n",
                timeoutLogCount_, timeoutLogCount_ == 1 ? "" : "s");
            timeoutLogCount_ = 0;
        }

        // Clean up any remaining clients when stopping.  Reset individual
        // slots rather than clearing the whole vector so that the
        // getFdCeiling()-sized pre-allocation from the constructor is
        // preserved across repeated run() calls.
        for (auto fd : clientFds_) {
            if (fd < clientSlots_.size() && clientSlots_[fd]) {
                onDisconnect(clientSlots_[fd]->data);
                clientSlots_[fd].reset();
            }
        }
        clientFds_.clear();
        current_poller_ = nullptr;
    }

    // Access the underlying listening socket (e.g. to set socket options).
    TcpSocket& getSocket() { return *listener_; }
    const TcpSocket& getSocket() const { return *listener_; }

    // Current number of connected clients.
    size_t clientCount() const { return clientFds_.size(); }

    // Peak concurrent client count since server started.
    size_t peakClientCount() const { return peak_clients_; }

    // Mark a client socket as active (resets the keep-alive idle timer).
    // Call this after a successful read or write.
    //
    // How this interacts with the timeout heap:
    //   We update lastActivity in the ClientEntry, then push a *new*
    //   TimeoutEntry with the refreshed expiry.  The old entry stays in the
    //   heap but is treated as stale by sweepTimeouts() because its
    //   lastActivitySnap will no longer match the ClientEntry's lastActivity.
    //   This is the core of the lazy-deletion strategy: O(log n) push, zero
    //   removal cost.
    //
    //   To prevent unbounded heap growth under high-throughput workloads
    //   (e.g. wrk with 15 000 keep-alive connections), we only push a new
    //   entry when at least keepAliveTimeout_/4 has elapsed since the last
    //   push for this client.  This bounds the heap to roughly
    //   O(clients * 4) entries instead of O(clients * requests_per_second *
    //   keepAliveTimeout).  Worst-case timeout accuracy is +25%.
    void touchClient(const TcpSocket& sock) {
        const uintptr_t fd = sock.getNativeHandle();
        ClientEntry* ce = findClient(fd);
        if (!ce) return;

        const auto now = SteadyClock::now();
        ce->lastActivity = now;

        // Only bother pushing if keep-alive timeouts are enabled.
        if (keepAliveTimeout_.count() > 0) {
            const auto sincePush = now - ce->lastTimeoutPush;
            if (sincePush >= keepAliveTimeout_ / 4) {
                pushTimeoutEntry(fd, now);
                ce->lastTimeoutPush = now;
            }
        }
    }

    // Enable or disable Writable interest for a client socket.
    // Call with true when you have data to send, false when done sending.
    void setClientWritable(const TcpSocket& sock, bool writable) {
        if (!current_poller_) return;
        PollEvent interest = PollEvent::Readable | PollEvent::Error;
        if (writable) interest = interest | PollEvent::Writable;
        current_poller_->modify(sock, interest);
    }

    // Keep-alive idle timeout. Connections that have been idle longer than
    // this will be closed gracefully. Set to 0 to disable. Default: 65000ms.
    // Accepts milliseconds so callers can specify sub-second timeouts without
    // the silent truncation-to-zero that chrono::seconds would cause.
    void setKeepAliveTimeout(std::chrono::milliseconds timeout) {
        keepAliveTimeout_ = timeout;
    }
    std::chrono::milliseconds getKeepAliveTimeout() const {
        return keepAliveTimeout_;
    }

    // Request a graceful shutdown. Safe to call from any thread.
    // run() will exit after the current wait() returns.
    void requestStop() noexcept {
        stop_.store(true, std::memory_order_relaxed);
    }
    bool stopRequested() const noexcept {
        return stop_.load(std::memory_order_relaxed);
    }

    // Control whether run() installs its own SIGINT/SIGTERM handlers.
    //
    // Default: true (signal handling enabled).
    //
    // When enabled, SIGINT/SIGTERM set a process-wide static flag that causes
    // every ServerBase whose handleSignals_ is true to stop on the next loop
    // iteration.  Set to false on secondary servers (or servers running in
    // threads) that should be stopped via requestStop() instead.
    void setHandleSignals(bool enable) noexcept { handleSignals_ = enable; }
    bool handlesSignals() const noexcept { return handleSignals_; }

    protected:
    // -- Override in derived classes ------------------------------------------

    // Called when the client socket is readable.
    virtual ServerResult onReadable(TcpSocket& sock, ClientData& data) = 0;

    // Called when the client socket is writable.
    virtual ServerResult onWritable(TcpSocket& sock, ClientData& data) = 0;

    // Called just before a client is removed.  Default: no-op.
    virtual void onDisconnect(ClientData& /*data*/) {}

    // Called when a poll error event fires on a client socket.
    // sock is still valid at this point.
    // Return KeepConnection to leave the client registered (e.g. the error
    // was transient), Disconnect to tear down this client (the default), or
    // StopServer to halt the server gracefully.
    virtual ServerResult onError(TcpSocket& /*sock*/, ClientData& /*data*/) {
        return ServerResult::Disconnect;
    }

    // Called after the keep-alive sweep closes one or more idle connections.
    // Default: accumulates count and prints at most once per minute.
    virtual void onClientsTimedOut(size_t count) {
        timeoutLogCount_ += count;
        auto now = SteadyClock::now();
        if (now - timeoutLogLast_ >= std::chrono::minutes{1}) {
            printf(
                "[keepalive] closed %zu idle connection%s in the last minute\n",
                timeoutLogCount_, timeoutLogCount_ == 1 ? "" : "s");
            timeoutLogCount_ = 0;
            timeoutLogLast_ = now;
        }
    }

    // Called when a newly accepted socket cannot be registered with the poller
    // (a rare OS error — e.g. ENOMEM or the process fd table is full).
    // The socket has been accepted from the OS but cannot be served; it will
    // be closed immediately after this call returns.
    //
    // Parameters:
    //   fd       — the OS fd/SOCKET handle of the accepted (but unserved)
    //   socket. peerAddr — remote address string ("a.b.c.d:port"), empty if
    //   unavailable.
    //
    // The TcpSocket itself is NOT passed because the callee cannot usefully
    // send or receive on it (the poller never registered it).  Read the peer
    // address for logging/metrics; the socket is closed immediately on return.
    //
    // Default: logs a warning to stderr.
    // Override to suppress the log, record metrics, or increment a counter.
    virtual void onPollerAddFailed(uintptr_t fd, const std::string& peerAddr) {
        fprintf(stderr,
            "[ServerBase] warning: poller.add failed for accepted fd %zu"
            " (peer %s) — connection dropped\n",
            static_cast<size_t>(fd), peerAddr.c_str());
    }

    // Called when poller.wait() returns with no ready events, i.e. a genuine
    // poll timeout.  Use this for periodic bookkeeping on the server thread
    // without spawning extra threads.
    //
    // For reliable periodic calls, pass a bounded timeout to run()
    // (e.g. Milliseconds{100}).  onIdle() is NOT called when events fire,
    // so no per-callback timer guard is necessary.
    //
    // Note: keep-alive timeout enforcement is handled entirely by
    // sweepTimeouts() which runs in the main loop automatically; there is
    // no need to call ServerBase::onIdle() from overrides.
    virtual ServerResult onIdle() { return ServerResult::KeepConnection; }

    private:
    using SteadyClock = std::chrono::steady_clock;

    // Keepalive log throttle state — initialised to epoch so the first
    // sweep always prints immediately rather than waiting a full minute.
    SteadyClock::time_point timeoutLogLast_{};
    size_t timeoutLogCount_{0};
    struct ClientEntry {
        std::unique_ptr<TcpSocket> socket;
        ClientData data;
        SteadyClock::time_point lastActivity{SteadyClock::now()};
        SteadyClock::time_point lastTimeoutPush{}; // throttle heap pushes
        size_t activeIdx{0}; // index into clientFds_ for O(1) erase

        ClientEntry() = default;
        ClientEntry(const ClientEntry&) = delete;
        ClientEntry& operator=(const ClientEntry&) = delete;

        ClientEntry(ClientEntry&& other) noexcept
            : socket(std::move(other.socket))
            , data(std::move(other.data))
            , lastActivity(other.lastActivity)
            , lastTimeoutPush(other.lastTimeoutPush)
            , activeIdx(other.activeIdx) {}

        ClientEntry(std::unique_ptr<TcpSocket> sock)
            : socket(std::move(sock)), data() {}

        ~ClientEntry() = default;
    };

    // -----------------------------------------------------------------------
    // TimeoutEntry: one node in the lazy-deletion min-heap.
    //
    // Design -- why lazy deletion?
    //   A keep-alive sweep must visit only the clients that have *actually*
    //   expired, not all connected clients.  A sorted structure (heap, set)
    //   lets us stop as soon as the front entry hasn't expired yet.
    //
    //   The tricky part is that touchClient() *updates* a client's expiry.
    //   Rather than finding and moving the old heap node (which would require
    //   an index and O(log n) decrease-key), we simply push a *new* entry
    //   with the refreshed expiry and leave the old one in place.
    //
    //   On each sweep we pop entries in expiry order and discard any that are
    //   stale before touching the socket:
    //     - fd no longer in clients_     -> client already gone, skip.
    //     - lastActivitySnap != current   -> client was touched after this
    //                                      entry was pushed; a newer entry
    //                                      exists further back in the heap.
    //
    //   Cost: O(1) when nothing has expired (single comparison at front).
    //         O(k log n) for k genuine + stale entries popped per sweep.
    //         O(log n) per touchClient() call.
    // -----------------------------------------------------------------------
    struct TimeoutEntry {
        SteadyClock::time_point expiry; // absolute time this fires
        SteadyClock::time_point lastActivitySnap; // lastActivity when pushed;
                                                  //   stale if it differs from
                                                  //   the ClientEntry's value
        uintptr_t fd; // key into clients_

        // operator< is intentionally inverted relative to wall-clock order.
        // std::push_heap / std::pop_heap build a MAX-heap (largest value at
        // front).  By making "larger expiry" compare as less-than we make
        // "smaller expiry" sort as the largest value -- so the soonest-to-
        // expire entry stays at the front, giving us a min-heap by expiry
        // with no extra comparator object anywhere.
        bool operator<(const TimeoutEntry& o) const noexcept {
            return expiry
                > o.expiry; // reversed: earlier expiry = higher priority
        }
    };

    // Push a new TimeoutEntry for 'fd' onto the heap.
    // Called on accept (initial entry) and on every touchClient() call.
    // Older entries for the same fd stay in the heap; they are lazily
    // identified as stale inside sweepTimeouts() at no extra cost here.
    void pushTimeoutEntry(uintptr_t fd, SteadyClock::time_point activity) {
        // Append to the back of the vector (O(1) amortised).
        timeout_heap_.push_back({activity + keepAliveTimeout_, activity, fd});
        // Bubble the new element up to restore the heap invariant.
        // Uses operator< above, so the min-expiry element ends up at front.
        std::push_heap(timeout_heap_.begin(), timeout_heap_.end());
    }

    // Drain all expired entries from the front of the timeout_heap_ and
    // close the corresponding connections.
    //
    // Called every loop iteration.  The fast-path (nothing expired) costs
    // one time comparison against the heap front and then returns -- O(1).
    void sweepTimeouts(Poller& poller) {
        if (keepAliveTimeout_.count() == 0 || timeout_heap_.empty()) return;

        const auto now = SteadyClock::now();

        // Fast-path: the soonest-expiring entry in the whole heap hasn't
        // fired yet, so nothing else can have fired either.  Return now.
        if (timeout_heap_.front().expiry > now) return;

        size_t closedCount = 0;

        // Pop entries one by one until we reach an unexpired one or exhaust
        // the heap.  Each iteration is O(log n) for the re-heapify.
        while (!timeout_heap_.empty() && timeout_heap_.front().expiry <= now) {
            // Copy the front entry out before popping.
            // (pop_heap moves front to back, then re-heapifies [begin, end-1)
            //  so we must read it *before* calling pop_heap.)
            TimeoutEntry entry = timeout_heap_.front();
            std::pop_heap(timeout_heap_.begin(), timeout_heap_.end());
            timeout_heap_.pop_back(); // remove the element now sitting at back

            // ---- Stale-entry check 1 ----------------------------------------
            // The client was already removed via a non-timeout path (e.g. a
            // read error, or the remote half closed).  The heap entry is a
            // dangling reference -- discard it.
            ClientEntry* ce = findClient(entry.fd);
            if (!ce) continue;

            // ---- Stale-entry check 2 ----------------------------------------
            // touchClient() was called after this entry was pushed, updating
            // lastActivity in the ClientEntry and pushing a newer heap entry.
            // Our snapshot no longer matches — a more recent expiry entry
            // exists further back in the heap.  Discard this stale entry.
            if (ce->lastActivity != entry.lastActivitySnap) continue;

            // ---- Genuine timeout: close the idle connection. ----------------
            onDisconnect(ce->data);
            ce->socket->shutdown(ShutdownHow::Both);
            (void)poller.remove(*ce->socket);
            eraseClient(entry.fd);
            ++closedCount;
        }

        if (closedCount > 0) onClientsTimedOut(closedCount);
    }

    std::atomic<bool> stop_{false};
#ifdef SERVER_STATS
    size_t max_clients_{0};
#endif

    // Returns the process's current soft fd limit as the sparse-array ceiling.
    // Called once in run() to pre-size clientSlots_ before any accept().
    // On POSIX this is getrlimit(RLIMIT_NOFILE).  On Windows, SOCKET handles
    // are opaque UINT_PTR values; in practice they start small, but we cap at
    // 65 536 which comfortably exceeds the default WSA socket limit.
    static size_t getFdCeiling() noexcept {
#ifdef _WIN32
        return 65536;
#else
        struct rlimit rl{};
        if (::getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_cur != RLIM_INFINITY
            && rl.rlim_cur > 0)
            return static_cast<size_t>(rl.rlim_cur);
        return 65536; // safe fallback
#endif
    }

    bool handleSignals_{true}; // per-instance opt-out; see setHandleSignals()
    Poller* current_poller_{nullptr};
    std::unique_ptr<TcpSocket> listener_;
    // Flat O(1)-by-fd client table.  Replaces unordered_map to eliminate
    // hashing, pointer chasing, and rehash pauses under load.
    // clientSlots_: sparse vector indexed by fd; non-null means active.
    // clientFds_:   dense list of live fds for fast iteration and count.
    std::vector<std::unique_ptr<ClientEntry>> clientSlots_;
    std::vector<uintptr_t> clientFds_;
    // Throttle: record when sweepTimeouts last ran.
    SteadyClock::time_point lastSweepTime_{};
    // Vector maintained as a min-heap (by expiry).  Pre-reserved to avoid
    // reallocations during high-frequency touchClient() churn.  The heap
    // may contain stale entries (lazily removed during sweepTimeouts()).
    std::vector<TimeoutEntry> timeout_heap_;
    std::chrono::milliseconds keepAliveTimeout_{65'000};
    size_t peak_clients_{0};

    // --- Flat client map helpers
    // ----------------------------------------------- O(1) by-fd lookup.
    ClientEntry* findClient(uintptr_t fd) {
        if (fd < clientSlots_.size()) return clientSlots_[fd].get();
        return nullptr;
    }

    // O(1) amortised insert.  Stores activeIdx for O(1) future erase.
    ClientEntry& emplaceClient(uintptr_t fd, std::unique_ptr<TcpSocket> sock) {
        // clientSlots_ is pre-sized in the constructor to getFdCeiling(), so
        // the OS should never hand us an fd that falls outside the array.
        // Assert in debug builds to catch any violation immediately; the
        // resize below is an absolute last-resort safety net (e.g. the app
        // called setrlimit() to raise the limit after construction).
        assert(fd < clientSlots_.size()
            && "emplaceClient: fd exceeds getFdCeiling() — "
               "was setrlimit() called after ServerBase construction?");
        if (fd >= clientSlots_.size()) clientSlots_.resize(fd + 1);
        clientSlots_[fd] = std::make_unique<ClientEntry>(std::move(sock));
        clientSlots_[fd]->activeIdx = clientFds_.size();
        clientFds_.push_back(fd);
        return *clientSlots_[fd];
    }

    // O(1) erase: swap-and-pop the fd from clientFds_, update the swapped
    // entry's activeIdx, then reset the slot.
    void eraseClient(uintptr_t fd) {
        if (fd >= clientSlots_.size() || !clientSlots_[fd]) return;
        const size_t idx = clientSlots_[fd]->activeIdx;
        if (idx + 1 < clientFds_.size()) {
            const uintptr_t lastFd = clientFds_.back();
            clientFds_[idx] = lastFd;
            if (lastFd < clientSlots_.size() && clientSlots_[lastFd])
                clientSlots_[lastFd]->activeIdx = idx;
        }
        clientFds_.pop_back();
        clientSlots_[fd].reset();
    }
    // ---------------------------------------------------------------------------

    void drainAccept(Poller& poller, bool& accepting, size_t& accepted,
        ClientLimit maxClients) {
        for (;;) {
            auto client = listener_->accept();
            if (!client) {
                // WouldBlock or hard error -- either way stop draining.
                break;
            }
            // Non-blocking mode and TCP_NODELAY are already propagated from
            // the listener by SocketImpl::propagateSocketProps (called by
            // accept()).  Buffer sizes are set on the listener at construction
            // time and also propagated.  Nothing extra needed here.

            uintptr_t key = client->getNativeHandle();
            if (!poller.add(*client, PollEvent::Readable | PollEvent::Error)) {
                // Capture peer address before the socket is destroyed.
                std::string peer;
                auto ep = client->getPeerEndpoint();
                if (ep.isSuccess()) peer = ep.value().toString();
                onPollerAddFailed(key, peer);
                // client goes out of scope here — socket closed via RAII.
                continue;
            }

            ClientEntry& ce = emplaceClient(key, std::move(client));
            ++accepted;
            if (clientFds_.size() > peak_clients_)
                peak_clients_ = clientFds_.size();

            // Push the first timeout entry for this new connection.
            if (keepAliveTimeout_.count() > 0)
                pushTimeoutEntry(key, ce.lastActivity);

#ifdef SERVER_STATS
            printf("[stats] clients: %zu  peak: %zu\n", clientFds_.size(),
                peak_clients_);
#endif

            if (maxClients != ClientLimit::Unlimited
                && accepted >= static_cast<size_t>(maxClients)) {
                (void)poller.remove(*listener_);
                accepting = false;
                break;
            }
        }
    }
};

} // namespace aiSocks

#endif // AISOCKS_SERVER_BASE_H
