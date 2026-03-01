// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
#ifndef AISOCKS_SERVER_BASE_H
#define AISOCKS_SERVER_BASE_H

#include "Poller.h"
#include "TcpSocket.h"
#include "SocketFactory.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <vector>

namespace aiSocks {

// Client connection limits with sensible defaults and maximums
enum class ClientLimit : size_t {
    Unlimited = 0, // Accept unlimited connections
    Default = 1000, // Default limit for production safety
    Low = 100, // Low resource environments
    Medium = 500, // Medium resource environments
    High = 2000, // High performance servers
    Maximum = 10000 // Reasonable maximum for most systems
};

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
//     Called on every loop iteration after poller.wait() returns, before
//     processing events. Default returns KeepConnection.
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
//   srv.run();
//
// Throws SocketException if bind, listen, or Poller registration fails.
// run() blocks until there are no more clients and accepting has stopped.
// ---------------------------------------------------------------------------

template <typename ClientData> class ServerBase {
    public:
    // Construct and start listening.  Does not accept until run() is called.
    // Returns invalid server if bind or listen fails - check isValid().
    explicit ServerBase(
        const ServerBind& args, AddressFamily family = AddressFamily::IPv4)
        : listener_(std::make_unique<TcpSocket>(TcpSocket::createRaw(family))) {
        // Use SocketFactory to create server without exceptions
        auto result = SocketFactory::createTcpServer(family, args);
        if (result.isSuccess()) {
            *listener_ = std::move(result.value());
            // Non-blocking is set by default (library default: all sockets
            // start non-blocking).  Set server-wide policies on the listener
            // so accepted sockets inherit them via propagateSocketProps.
            if (!listener_->setNoDelay(true))
                printf("Warning: Failed to set TCP_NODELAY on server socket\n");
            (void)listener_->setReceiveBufferSize(256 * 1024);
            (void)listener_->setSendBufferSize(256 * 1024);
        } else {
            // Server creation failed - socket remains invalid
            listener_.reset();
        }
    }

    virtual ~ServerBase() {
        // Clean up any remaining clients before destruction.
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
        auto prevInt = std::signal(SIGINT, handleSignal);
        auto prevTerm = std::signal(SIGTERM, handleSignal);
        struct SigGuard {
            void (*pi)(int);
            void (*pt)(int);
            ~SigGuard() {
                std::signal(SIGINT, pi);
                std::signal(SIGTERM, pt);
            }
        } guard{prevInt, prevTerm};

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
            clientSlots_.reserve(cap + 64); // fds grow from small base
            clientFds_.reserve(cap);
            // Each client can have multiple heap entries while stale ones
            // accumulate between sweeps, so reserve 2x the client capacity.
            timeout_heap_.reserve(cap * 2);
        }

        bool accepting = true;
        size_t accepted = 0;

        while (!stop_.load(std::memory_order_relaxed)
            && !signal_stop_.load(std::memory_order_relaxed)
            && (accepting || !clientFds_.empty())) {
            auto ready = poller.wait(timeout);
            if (stop_.load(std::memory_order_relaxed)
                || signal_stop_.load(std::memory_order_relaxed))
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
                    onError(*ce->socket, ce->data);
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

            if (onIdle() == ServerResult::StopServer) {
                stop_.store(true);
                break;
            }
        }

        // Flush any accumulated keep-alive timeout count before exiting.
        if (timeoutLogCount_ > 0) {
            printf("[keepalive] closed %zu idle connection%s total\n",
                timeoutLogCount_, timeoutLogCount_ == 1 ? "" : "s");
            timeoutLogCount_ = 0;
        }

        // Clean up any remaining clients when stopping.
        for (auto fd : clientFds_) {
            if (fd < clientSlots_.size() && clientSlots_[fd])
                onDisconnect(clientSlots_[fd]->data);
        }
        clientFds_.clear();
        clientSlots_.clear();
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
    void touchClient(const TcpSocket& sock) {
        const uintptr_t fd = sock.getNativeHandle();
        ClientEntry* ce = findClient(fd);
        if (!ce) return;

        const auto now = SteadyClock::now();
        ce->lastActivity = now;

        // Only bother pushing if keep-alive timeouts are enabled.
        if (keepAliveTimeout_.count() > 0) pushTimeoutEntry(fd, now);
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

    // Optimized send with large chunks for better throughput
    int sendOptimized(TcpSocket& sock, const char* data, size_t size) {
        const size_t CHUNK_SIZE = 64 * 1024; // 64KB chunks
        size_t sent = 0;

        while (sent < size) {
            size_t to_send = std::min(CHUNK_SIZE, size - sent);
            int n = sock.send(data + sent, to_send);
            if (n <= 0) {
                return sent > 0 ? static_cast<int>(sent) : n;
            }
            sent += n;

            // If we couldn't send the full chunk, socket buffer is full
            if (n < static_cast<int>(to_send)) {
                break;
            }
        }

        return static_cast<int>(sent);
    }

    // Request a graceful shutdown. Safe to call from any thread.
    // run() will exit after the current wait() returns.
    void requestStop() noexcept {
        stop_.store(true, std::memory_order_relaxed);
    }
    bool stopRequested() const noexcept {
        return stop_.load(std::memory_order_relaxed);
    }

    protected:
    // -- Override in derived classes ------------------------------------------

    // Called when the client socket is readable.
    virtual ServerResult onReadable(TcpSocket& sock, ClientData& data) = 0;

    // Called when the client socket is writable.
    virtual ServerResult onWritable(TcpSocket& sock, ClientData& data) = 0;

    // Called just before a client is removed.  Default: no-op.
    virtual void onDisconnect(ClientData& /*data*/) {}

    // Called when a poll error event fires on a client socket.
    // sock is still valid at this point. Default: no-op.
    virtual void onError(TcpSocket& /*sock*/, ClientData& /*data*/) {}

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

    // Called on every loop iteration after poller.wait() returns, before
    // processing events.  Use this hook for periodic bookkeeping on the
    // server thread without spawning extra threads.  For reliable periodic
    // calls pass a bounded timeout to run() (e.g. Milliseconds{100}).
    //
    // Note: keep-alive timeout enforcement is now handled entirely by
    // sweepTimeouts() which runs in the main loop automatically -- there is
    // no need to call ServerBase::onIdle() from overrides any more.
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
        size_t activeIdx{0}; // index into clientFds_ for O(1) erase

        ClientEntry() = default;
        ClientEntry(const ClientEntry& other)
            : socket(other.socket ? std::make_unique<TcpSocket>(*other.socket)
                                  : nullptr)
            , data(other.data)
            , lastActivity(other.lastActivity)
            , activeIdx(other.activeIdx) {}

        ClientEntry(ClientEntry&& other) noexcept
            : socket(std::move(other.socket))
            , data(std::move(other.data))
            , lastActivity(other.lastActivity)
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

    static void handleSignal(int) {
        // Signal handler can't access instance, so use a static flag for
        // signals only This is only used for Ctrl+C, not for normal test
        // shutdown
        signal_stop_.store(true, std::memory_order_relaxed);
    }

    static std::atomic<bool> signal_stop_;
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

// Define static member
template <typename ClientData>
std::atomic<bool> ServerBase<ClientData>::signal_stop_{false};

} // namespace aiSocks

#endif // AISOCKS_SERVER_BASE_H
