// SPDX-FileCopyrightText: 2025-2026 nomos-studio contributors
//
// SPDX-License-Identifier: EPL-2.0

#pragma once

#include <nomos/rt/signal_handlers.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

namespace nomos::rt {

// Start a daemon thread that emits "[heartbeat]\n" to stderr every
// interval_ms milliseconds for as long as *running is true.
//
// The supervising BEAM Port captures this via :stderr_to_stdout and uses it
// to detect hung processes: a process that stops heartbeating but has not
// exited is killed and restarted by the supervisor.
//
// Returns a joinable thread.  Returns a default-constructed (non-joinable)
// thread when interval_ms == 0 so callers can unconditionally join().
inline std::thread start_heartbeat_thread(uint32_t                 interval_ms,
                                          const std::atomic<bool>& running) noexcept {
    if (interval_ms == 0)
        return {};
    return std::thread{[interval_ms, &running]() noexcept {
        block_signals_on_this_thread();
        while (running.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
            if (running.load(std::memory_order_relaxed)) {
                std::fputs("[heartbeat]\n", stderr);
                std::fflush(stderr);
            }
        }
    }};
}

} // namespace nomos::rt
