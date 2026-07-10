// SPDX-FileCopyrightText: 2025-2026 nomos-studio contributors
//
// SPDX-License-Identifier: EPL-2.0

#pragma once

#include <pthread.h>
#include <signal.h>

namespace nomos::rt {

// Install SIGINT/SIGTERM handlers via sigaction.  SA_RESTART is intentionally
// absent: blocking syscalls (accept, read) return EINTR on signal delivery so
// threads can re-check their running flag and exit cleanly.
//
// SIGPIPE is set to SIG_IGN so that writes to a closed peer return EPIPE
// instead of terminating the process.
//
// handler must be async-signal-safe (typically just stores to an atomic<bool>).
inline void install_signal_handlers(void (*handler)(int)) noexcept {
    struct sigaction sa {};
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    struct sigaction sa_pipe {};
    sa_pipe.sa_handler = SIG_IGN;
    sigemptyset(&sa_pipe.sa_mask);
    sa_pipe.sa_flags = 0;
    sigaction(SIGPIPE, &sa_pipe, nullptr);
}

// Block all signals on the calling thread.  Call from RT threads (audio
// callback, event dispatch, MIDI output) so that SIGTERM/SIGINT are delivered
// only to the main thread, which owns the running flag and shutdown sequence.
inline void block_signals_on_this_thread() noexcept {
    sigset_t all;
    sigfillset(&all);
    pthread_sigmask(SIG_BLOCK, &all, nullptr);
}

} // namespace nomos::rt
