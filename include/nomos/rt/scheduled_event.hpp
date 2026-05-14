// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <nomos/rt/input_event.hpp>
#include <nomos/rt/spsc_queue.hpp>

namespace nomos::rt {

// One beat-tagged event waiting to fire.
struct scheduled_event {
    double           beat; // Link beat at which to deliver the event
    clap_event_union event;
};

// Staging queue: written by rt_control_thread (control thread),
//               read by event_scheduler::tick() (audio/event thread).
// 1024 slots ≈ 24 KB; generous for any burst cljseq can produce at block rate.
constexpr std::size_t sched_queue_capacity = 1024;
using sched_staging_queue                  = spsc_queue<scheduled_event, sched_queue_capacity>;

} // namespace nomos::rt
