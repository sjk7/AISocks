// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#ifndef AISOCKS_POLL_EVENT_LOOP_H
#define AISOCKS_POLL_EVENT_LOOP_H

#include "Poller.h"
#include "ServerSignal.h"
#include "ServerTypes.h"
#include "TcpSocket.h"
#include <atomic>
#include <csignal>

namespace aiSocks {

// ---------------------------------------------------------------------------
// PollEventLoop
//
// Owns the Poller and signal handling for a poll-driven server.
// Responsible for exactly one thing: blocking on poller.wait(), dispatching
// socket events, and stopping cleanly on signal or explicit request.
//
// All other concerns (client lifecycle, keep-alive timeouts, load tuning)
// remain in ServerBase.
//
// run() is a template so that the three handler lambdas from ServerBase
// are monomorphised at the call site — zero overhead vs. writing the loop
// inline, with no heap allocation.
//
// Handler signatures:
//   EventHandler  : bool(TcpSocket&, PollEvent)
//                   Called once per ready socket per poller.wait() call.
//                   Return false to stop the loop immediately.
//   AfterBatchFn  : bool(bool idle)
//                   Called once per poller.wait() call, after all events have
//                   been dispatched.  `idle` is true when poller.wait() timed
//                   out with no events (a genuine idle tick).
//                   Return false to stop the loop.
//   StopPredicate : bool()
//                   Checked at the top of every iteration before wait().
//                   Return true to exit gracefully.
// ---------------------------------------------------------------------------
class PollEventLoop {
    public:
    explicit PollEventLoop() = default;

    // Non-copyable, non-movable (Poller is non-copyable and owns OS handles).
    PollEventLoop(const PollEventLoop&) = delete;
    PollEventLoop& operator=(const PollEventLoop&) = delete;
    PollEventLoop(PollEventLoop&&) = delete;
    PollEventLoop& operator=(PollEventLoop&&) = delete;

    // --- Poller delegation ---------------------------------------------------

    bool add(const Socket& s, PollEvent interest) {
        return poller_.add(s, interest);
    }
    bool modify(const Socket& s, PollEvent interest) {
        return poller_.modify(s, interest);
    }
    bool remove(const Socket& s) { return poller_.remove(s); }

    // --- Control -------------------------------------------------------------

    // Request the loop to stop after the current wait() returns.
    // Safe to call from any thread.
    void requestStop() noexcept {
        stop_.store(true, std::memory_order_relaxed);
    }

    bool stopRequested() const noexcept {
        return stop_.load(std::memory_order_relaxed);
    }

    // When true (the default), run() installs SIGINT/SIGTERM handlers that
    // set the process-wide g_serverSignalStop flag and restores the previous
    // handlers on return.  Set false for secondary servers (or servers on
    // worker threads) that should be stopped via requestStop() only.
    void setHandleSignals(bool enable) noexcept { handleSignals_ = enable; }
    bool handlesSignals() const noexcept { return handleSignals_; }

    // --- Main loop -----------------------------------------------------------

    template <typename EventHandler, typename AfterBatchFn,
        typename StopPredicate>
    void run(Milliseconds timeout,
        EventHandler&& onEvent, // bool(TcpSocket&, PollEvent)
        AfterBatchFn&& afterBatch, // bool(bool idle)
        StopPredicate&& shouldStop) // bool()
    {
        // Clear any stop flag left from a previous run() call so that
        // repeated run() invocations work correctly.  A requestStop() call
        // that arrives before run() starts is intentionally lost — if the
        // caller wants a no-op run, they should check stopRequested() first.
        stop_.store(false, std::memory_order_relaxed);

        // Install SIGINT/SIGTERM and restore them when run() returns.
        //
        // NOTE: g_serverSignalStop is a process-wide flag.  When two servers
        // run concurrently and both have handleSignals_ == true, SIGINT stops
        // both.  Use setHandleSignals(false) on secondary servers.
        using SigHandler = void (*)(int);
        SigHandler prevInt = SIG_DFL;
        SigHandler prevTerm = SIG_DFL;
        if (handleSignals_) {
            prevInt = std::signal(SIGINT, serverHandleSignal);
            prevTerm = std::signal(SIGTERM, serverHandleSignal);
        }
        // Restore whichever handler was installed *before* this run() call
        // (SIG_DFL, SIG_IGN, or another handler).  Restoring SIG_DFL blindly
        // would clobber a handler installed by the caller or a wrapper.
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

        while (true) {
            // Pre-wait stop checks: handles requestStop() arriving between
            // two iterations, signal delivery, and caller-side conditions
            // (e.g. no clients left and not accepting).
            if (stop_.load(std::memory_order_relaxed)) break;
            if (handleSignals_
                && g_serverSignalStop.load(std::memory_order_relaxed))
                break;
            if (shouldStop()) break;

            const auto& ready = poller_.wait(timeout);

            // Post-wait stop checks: handles requestStop() or a signal that
            // arrived while we were blocked inside poller_.wait().
            if (stop_.load(std::memory_order_relaxed)) break;
            if (handleSignals_
                && g_serverSignalStop.load(std::memory_order_relaxed))
                break;

            for (auto& ev : ready) {
                // PollResult::socket is a const Socket*, but callers need a
                // mutable TcpSocket&.  The Poller stores borrowed pointers to
                // objects owned by the caller, so this cast is safe.
                auto& sock = *const_cast<TcpSocket*>(
                    static_cast<const TcpSocket*>(ev.socket));
                if (!onEvent(sock, ev.events)) {
                    stop_.store(true, std::memory_order_relaxed);
                    break;
                }
            }
            // If onEvent() triggered a stop mid-batch (e.g. a StopServer
            // result from a virtual hook), skip afterBatch entirely — the
            // server is already tearing down and afterBatch must not run.
            // Note: shouldStop() is NOT re-checked during the event loop;
            // onEvent() returning false is the only intra-batch stop path.
            if (stop_.load(std::memory_order_relaxed)) break;

            // afterBatch runs once per wait() call — after all events have
            // been dispatched.  idle==true means poller.wait() timed out.
            if (!afterBatch(ready.empty())) {
                stop_.store(true, std::memory_order_relaxed);
                break;
            }
        }
    }

    private:
    Poller poller_;
    std::atomic<bool> stop_{false};
    bool handleSignals_{true};
};

} // namespace aiSocks

#endif // AISOCKS_POLL_EVENT_LOOP_H
