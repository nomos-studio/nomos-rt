// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <nomos/rt/input_event.hpp>
#include <nomos/rt/osc_event.hpp>
#include <nomos/rt/spsc_queue.hpp>

#include <cstdint>

namespace nomos::rt {

// Which payload a scheduled_event carries.
enum class sched_kind : std::uint8_t {
    clap, // CLAP note/MIDI event → fired via the tick callback (graph input)
    osc,  // outbound OSC datagram → routed to the scheduler's osc_out_queue
};

// One beat-tagged event waiting to fire. Trivially copyable so it rides the
// staging spsc_queue by value with no heap. Only the member selected by `kind`
// is meaningful; the other is left value-initialised.
struct scheduled_event {
    double           beat;                   // Link beat at which to deliver
    sched_kind       kind{sched_kind::clap}; // default keeps existing call sites clap
    clap_event_union event{};                // valid when kind == clap
    osc_event        osc{};                  // valid when kind == osc
};

// Staging queue: written by rt_control_thread (control thread),
//               read by event_scheduler::tick() (audio/event thread).
// 1024 slots ≈ 24 KB; generous for any burst nous can produce at block rate.
constexpr std::size_t sched_queue_capacity = 1024;
using sched_staging_queue                  = spsc_queue<scheduled_event, sched_queue_capacity>;

} // namespace nomos::rt
