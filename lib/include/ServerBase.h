// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#ifndef AISOCKS_SERVER_BASE_H
#define AISOCKS_SERVER_BASE_H

#include "KeepAliveTimeoutManager.h"
#include "PollEventLoop.h"
#include "Poller.h"
#include "ServerSignal.h"
#include "ServerTypes.h"
#include "TcpSocket.h"
#include "SocketFactory.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
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

    StopServer = -1, // Stop the server gracefully
    Disconnect = 0, // Disconnect this client
    KeepConnection = 1,
    OK = 1,
    Continue = 1, // Keep the connection alive/continue running the server

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
        Result<TcpSocket>* result = nullptr) {
        // Pre-size the sparse fd→client table to the process fd ceiling
        // here in the constructor, before any threading or accept() calls.
        // run() can then insert and erase clients in O(1) without ever
        // resizing clientSlots_ in the hot path.
        clientSlots_.resize(getFdCeiling());
        auto createResult = SocketFactory::createTcpServer(family, args);
        if (createResult.isSuccess()) {
            listener_
                = std::make_unique<TcpSocket>(std::move(createResult.value()));
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
            // listener_ is null (never assigned) — isValid() returns false.
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
    // timeout:    passed to Poller::wait(); INT64_MAX (wait_forever) blocks the
    //             poller until an event arrives; any other value <= 0 is
    //             clamped to 1ms (the minimum poll interval).
    //
    // Returns when there are no remaining connected clients (AND accepting is
    // stopped, either because maxClients was reached or you stopped
    // externally). Or via the thread-safe stop_ flag or if anything returns
    // ServerResult::StopServer from one of the, or if CTRL+C is detected.
    void run(ClientLimit maxClients = ClientLimit::Default,
        Milliseconds timeout = Milliseconds{-1}) {
        if (!isValid()) return;

        stop_.store(false, std::memory_order_relaxed);
        loop_.setHandleSignals(handleSignals_);

        if (!loop_.add(*listener_, PollEvent::Readable | PollEvent::Error))
            return;

        // Pre-reserve containers so the first burst of connections never
        // triggers reallocation in the hot path.
        {
            const size_t cap = (maxClients == ClientLimit::Unlimited)
                ? defaultMaxClients
                : static_cast<size_t>(maxClients);
            if (clientSlots_.empty()) clientSlots_.resize(getFdCeiling());
            clientFds_.reserve(cap);
            timeouts_.reserve(cap);
        }

        bool accepting = true;
        size_t accepted = 0;

        onReady();

        loop_.run(
            timeout,

            // --- EventHandler: dispatched once per ready socket --------------
            [&](TcpSocket& sock, PollEvent ev) -> bool {
                if (&sock == listener_.get()) {
                    if (accepting)
                        drainAccept(loop_, accepting, accepted, maxClients);
                    return true;
                }

                uintptr_t cfd = sock.getNativeHandle();
                ClientEntry* ce = findClient(cfd);
                if (!ce) return true;

                bool keep = !hasFlag(ev, PollEvent::Error);
                if (!keep) {
                    ServerResult r = onError(*ce->socket, ce->data);
                    if (r == ServerResult::StopServer) return false;
                    keep = (r == ServerResult::KeepConnection);
                }
                if (keep && hasFlag(ev, PollEvent::Readable)) {
                    ServerResult r = onReadable(*ce->socket, ce->data);
                    if (r == ServerResult::StopServer) return false;
                    keep = (r == ServerResult::KeepConnection);
                }
                if (keep && hasFlag(ev, PollEvent::Writable)) {
                    ServerResult r = onWritable(*ce->socket, ce->data);
                    if (r == ServerResult::StopServer) return false;
                    keep = (r == ServerResult::KeepConnection);
                }

                if (!keep) {
                    printf("[DEBUG] ServerBase disconnecting client fd=%llu\n",
                        (unsigned long long)cfd);
                    fflush(stdout);
                    onDisconnect(ce->data);
                    ce->socket->shutdown(ShutdownHow::Both);
                    (void)loop_.remove(*ce->socket);
                    eraseClient(cfd);

                    if (!accepting && maxClients != ClientLimit::Unlimited
                        && clientFds_.size()
                            < static_cast<size_t>(maxClients)) {
                        if (loop_.add(*listener_,
                                PollEvent::Readable | PollEvent::Error))
                            accepting = true;
                    }
#ifdef SERVER_STATS
                    printf("[stats] clients: %zu  peak: %zu\n",
                        clientFds_.size(), peak_clients_);
#endif
                }
                return true;
            },

            // --- AfterBatchFn: once per poller.wait() call -------------------
            // `idle` is true when no events fired (genuine timeout).
            [&](bool idle) -> bool {
                timeouts_.adjustForLoad(clientFds_.size());

                if (timeouts_.sweepDue(clientFds_.size())) {
                    ClientEntry* sweepCe = nullptr;
                    size_t closed = timeouts_.sweepRaw(
                        [&](uintptr_t fd)
                            -> std::pair<bool, SteadyClock::time_point> {
                            sweepCe = findClient(fd);
                            if (!sweepCe) return {false, {}};
                            return {true, sweepCe->lastActivity};
                        },
                        [&](uintptr_t fd) {
                            if (!sweepCe) return;
                            onDisconnect(sweepCe->data);
                            sweepCe->socket->shutdown(ShutdownHow::Both);
                            (void)loop_.remove(*sweepCe->socket);
                            eraseClient(fd);
                        });
                    if (closed > 0) onClientsTimedOut(closed);

                    if (!accepting && maxClients != ClientLimit::Unlimited
                        && clientFds_.size()
                            < static_cast<size_t>(maxClients)) {
                        if (loop_.add(*listener_,
                                PollEvent::Readable | PollEvent::Error))
                            accepting = true;
                    }
                }

                // onIdle() is called only on genuine timeouts (no events).
                if (idle) return onIdle() != ServerResult::StopServer;
                return true;
            },

            // --- StopPredicate -----------------------------------------------
            [&]() -> bool {
                return stop_.load(std::memory_order_relaxed)
                    || (!accepting && clientFds_.empty());
            });

        // Exit-reason logging.
        {
            const bool stoppedByFlag = stop_.load(std::memory_order_relaxed);
            const bool stoppedBySignal = handleSignals_
                && g_serverSignalStop.load(std::memory_order_relaxed);
            const bool noMore = !accepting && clientFds_.empty();
            const char* reason = stoppedByFlag ? "requestStop() was called"
                : stoppedBySignal
                ? "shutdown signal received (Ctrl+C / SIGTERM)"
                : noMore ? "client limit reached and all clients disconnected"
                         : "unknown";
            printf("\nServer stopped gracefully: %s.\n", reason);
        }

        if (timeoutLogCount_ > 0) {
            printf("[keepalive] closed %zu idle connection%s total\n",
                timeoutLogCount_, timeoutLogCount_ == 1 ? "" : "s");
            timeoutLogCount_ = 0;
        }

        // Reset individual slots (not clear()) so the pre-allocation from
        // the constructor is preserved across repeated run() calls.
        for (auto fd : clientFds_) {
            if (fd < clientSlots_.size() && clientSlots_[fd]) {
                onDisconnect(clientSlots_[fd]->data);
                clientSlots_[fd].reset();
            }
        }
        clientFds_.clear();
    }

    // Access the underlying listening socket (e.g. to set socket options).
    TcpSocket& getSocket() { return *listener_; }
    const TcpSocket& getSocket() const { return *listener_; }

    // The local endpoint (address + port) the server is bound to.
    // Returns an error Result if the socket is invalid.
    Result<Endpoint> serverEndpoint() const {
        return getSocket().getLocalEndpoint();
    }

    // The port the server is listening on (useful when binding to Port{0}).
    Port serverPort() const {
        auto ep = getSocket().getLocalEndpoint();
        return ep.isSuccess() ? ep.value().port : Port::any;
    }

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
    //   lastActivitySnap will no longer match the ClientEntry's
    //   lastActivity. This is the core of the lazy-deletion strategy: O(log
    //   n) push, zero removal cost.
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
        timeouts_.touch(fd, now, ce->lastTimeoutPush);
    }

    // Enable or disable Writable interest for a client socket.
    // Call with true when you have data to send, false when done sending.
    void setClientWritable(const TcpSocket& sock, bool writable) {
        PollEvent interest = PollEvent::Readable | PollEvent::Error;
        if (writable) interest = interest | PollEvent::Writable;
        loop_.modify(sock, interest);
    }

    // Keep-alive idle timeout. Connections that have been idle longer than
    // this will be closed gracefully. Set to 0 to disable. Default:
    // 65000ms. Uses the Milliseconds type so callers can specify sub-second
    // timeouts without the silent truncation-to-zero that chrono::seconds
    // would cause.
    void setKeepAliveTimeout(Milliseconds timeout) {
        timeouts_.setTimeout(std::chrono::milliseconds{timeout.count});
    }
    Milliseconds getKeepAliveTimeout() const {
        return Milliseconds{timeouts_.getTimeout().count()};
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
    // When enabled, SIGINT/SIGTERM set a process-wide static flag that
    // causes every ServerBase whose handleSignals_ is true to stop on the
    // next loop iteration.  Set to false on secondary servers (or servers
    // running in threads) that should be stopped via requestStop() instead.
    void setHandleSignals(bool enable) noexcept {
        handleSignals_ = enable;
        loop_.setHandleSignals(enable);
    }
    bool handlesSignals() const noexcept { return handleSignals_; }

    protected:
    // -- Override in derived classes
    // ------------------------------------------

    // Called when the client socket is readable.
    virtual ServerResult onReadable(TcpSocket& sock, ClientData& data) = 0;

    // Called when the client socket is writable.
    virtual ServerResult onWritable(TcpSocket& sock, ClientData& data) = 0;

    // Called just before a client is removed.  Default: no-op.
    virtual void onDisconnect(ClientData& /*data*/) {}

    // Called once the server has been added to the poller and is ready to
    // accept connections.  Override to signal waiting threads instead of
    // using a fixed sleep.
    virtual void onReady() {}

    // Called on the server thread immediately after a new client has been
    // accepted and registered. Override in subclasses that need a thread-safe
    // client-count observable from outside threads.
    virtual void onClientConnected(TcpSocket& /*sock*/) {}

    // Called on the server thread immediately after a client has been removed
    // from the active set (before the socket is closed).
    virtual void onClientDisconnected() {}

    // Called when a poll error event fires on a client socket.
    // sock is still valid at this point.
    // Return KeepConnection to leave the client registered (e.g. the error
    // was transient), Disconnect to tear down this client (the default), or
    // StopServer to halt the server gracefully.
    virtual ServerResult onError(TcpSocket& /*sock*/, ClientData& /*data*/) {
        return ServerResult::Disconnect;
    }

    // Called after the keep-alive sweep closes one or more idle
    // connections. Default: accumulates count and prints at most once per
    // minute.
    virtual void onClientsTimedOut(size_t count) {
        timeoutLogCount_ += count;
        auto now = SteadyClock::now();
        if (now - timeoutLogLast_ >= std::chrono::minutes{1}) {
            printf("[keepalive] closed %zu idle connection%s in the last "
                   "minute\n",
                timeoutLogCount_, timeoutLogCount_ == 1 ? "" : "s");
            timeoutLogCount_ = 0;
            timeoutLogLast_ = now;
        }
    }

    // Called when a newly accepted socket cannot be registered with the
    // poller (a rare OS error — e.g. ENOMEM or the process fd table is
    // full). The socket has been accepted from the OS but cannot be served;
    // it will be closed immediately after this call returns.
    //
    // Parameters:
    //   fd       — the OS fd/SOCKET handle of the accepted (but unserved)
    //   socket. peerAddr — remote address string ("a.b.c.d:port"), empty if
    //   unavailable.
    //
    // The TcpSocket itself is NOT passed because the callee cannot usefully
    // send or receive on it (the poller never registered it).  Read the
    // peer address for logging/metrics; the socket is closed immediately on
    // return.
    //
    // Default: logs a warning to stderr.
    // Override to suppress the log, record metrics, or increment a counter.
    virtual void onPollerAddFailed(uintptr_t fd, const std::string& peerAddr) {
        fprintf(stderr,
            "[ServerBase] warning: poller.add failed for accepted fd %zu"
            " (peer %s) — connection dropped\n",
            fd, peerAddr.c_str());
    }

    // Called when poller.wait() returns with no ready events, i.e. a
    // genuine poll timeout.  Use this for periodic bookkeeping on the
    // server thread without spawning extra threads.
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

        ClientEntry& operator=(ClientEntry&& other) noexcept {
            if (this != &other) {
                socket = std::move(other.socket);
                data = std::move(other.data);
                lastActivity = other.lastActivity;
                lastTimeoutPush = other.lastTimeoutPush;
                activeIdx = other.activeIdx;
            }
            return *this;
        }

        ClientEntry(std::unique_ptr<TcpSocket> sock)
            : socket(std::move(sock)), data() {}

        ~ClientEntry() = default;
    };

    std::atomic<bool> stop_{false};
#ifdef SERVER_STATS
    size_t max_clients_{0};
#endif

    // Returns the process's current soft fd limit as the sparse-array
    // ceiling. Called once in run() to pre-size clientSlots_ before any
    // accept(). On POSIX this is getrlimit(RLIMIT_NOFILE).  On Windows,
    // SOCKET handles are opaque UINT_PTR values; in practice they start
    // small, but we cap at 65 536 which comfortably exceeds the default WSA
    // socket limit.
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
    PollEventLoop loop_;
    std::unique_ptr<TcpSocket> listener_;
    // Flat O(1)-by-fd client table.  Replaces unordered_map to eliminate
    // hashing, pointer chasing, and rehash pauses under load.
    // clientSlots_: sparse vector indexed by fd; non-null means active.
    // clientFds_:   dense list of live fds for fast iteration and count.
    std::vector<std::unique_ptr<ClientEntry>> clientSlots_;
    std::vector<uintptr_t> clientFds_;
    KeepAliveTimeoutManager timeouts_;
    size_t peak_clients_{0};

    // --- Flat client map helpers
    // ----------------------------------------------- O(1) by-fd lookup.
    ClientEntry* findClient(uintptr_t fd) {
        if (fd < clientSlots_.size()) return clientSlots_[fd].get();
        return nullptr;
    }

    // O(1) amortised insert.  Stores activeIdx for O(1) future erase.
    ClientEntry& emplaceClient(uintptr_t fd, std::unique_ptr<TcpSocket> sock) {
        // clientSlots_ is pre-sized in the constructor to getFdCeiling(),
        // so the OS should never hand us an fd that falls outside the
        // array. Assert in debug builds to catch any violation immediately;
        // the resize below is an absolute last-resort safety net (e.g. the
        // app called setrlimit() to raise the limit after construction).
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
        onClientDisconnected();
        clientSlots_[fd].reset();
    }
    // ---------------------------------------------------------------------------

    void drainAccept(PollEventLoop& loop, bool& accepting, size_t& accepted,
        ClientLimit maxClients) {
        for (;;) {
            auto client = listener_->accept();
            if (!client) {
                // WouldBlock or hard error -- either way stop draining.
                break;
            }
            // Non-blocking mode and TCP_NODELAY are already propagated from
            // the listener by SocketImpl::propagateSocketProps (called by
            // accept()).  Buffer sizes are set on the listener at
            // construction time and also propagated.  Nothing extra needed
            // here.

            uintptr_t key = client->getNativeHandle();
            if (!loop.add(*client, PollEvent::Readable | PollEvent::Error)) {
                // Capture peer address before the socket is destroyed.
                std::string peer;
                auto ep = client->getPeerEndpoint();
                if (ep.isSuccess()) peer = ep.value().toString();
                onPollerAddFailed(key, peer);
                // client goes out of scope here — socket closed via RAII.
                continue;
            }

            ClientEntry& ce = emplaceClient(key, std::move(client));
            onClientConnected(*ce.socket);
            ++accepted;
            if (clientFds_.size() > peak_clients_)
                peak_clients_ = clientFds_.size();

            timeouts_.onAccept(key, ce.lastActivity);

#ifdef SERVER_STATS
            printf("[stats] clients: %zu  peak: %zu\n", clientFds_.size(),
                peak_clients_);
#endif

            if (maxClients != ClientLimit::Unlimited
                && clientFds_.size() >= static_cast<size_t>(maxClients)) {
                (void)loop.remove(*listener_);
                accepting = false;
                break;
            }
        }
    }
};

} // namespace aiSocks

#endif // AISOCKS_SERVER_BASE_H
