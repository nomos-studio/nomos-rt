// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <nomos/rt/scheduled_event.hpp>

#include <algorithm>
#include <vector>

namespace nomos::rt {

// Beat-quantized event scheduler — part of cljseq-rt.
//
// Control thread pushes beat-tagged events via staging().
// Audio/event thread fires due events each tick via tick(current_beat, fn).
//
// Threading model: one producer (control thread via push), one consumer
// (audio/event thread via tick).  The staging spsc_queue is lock-free.
// The pending_ vector is private to the consumer thread.
//
// Precision: events fire at the first tick where current_beat >= event.beat.
// Sub-block sample accuracy (computing the exact sample offset within a block)
// is a future enhancement — sample_offset is implicitly 0 for now, meaning
// events fire at the start of the block in which they become due.  This is
// already a large improvement over immediate IPC dispatch since it absorbs
// network and thread-scheduling jitter.
//
// Past-due events (beat already passed when tick is called) fire immediately
// rather than being dropped.  This handles startup lag and rare timing slip.
class event_scheduler {
  public:
    // Returns the staging queue to pass to rt_control_thread::config.
    sched_staging_queue& staging() noexcept { return staging_; }

    // Fire all events with beat <= current_beat.
    // fn: (const clap_event_union&) → void, called in beat order.
    // Call once per audio block or event-loop iteration.
    template <typename F> void tick(double current_beat, F&& fn) {
        // Drain new arrivals from the control thread into our local list.
        while (auto ev = staging_.pop())
            pending_.push_back(*ev);

        if (pending_.empty())
            return;

        // Keep pending_ sorted by beat so we can stop early at current_beat.
        std::stable_sort(
            pending_.begin(), pending_.end(),
            [](const scheduled_event& a, const scheduled_event& b) { return a.beat < b.beat; });

        auto it = pending_.begin();
        while (it != pending_.end() && it->beat <= current_beat) {
            fn(it->event);
            ++it;
        }
        pending_.erase(pending_.begin(), it);
    }

    // Number of events currently waiting (approximate — pending_ only,
    // staging may hold additional events not yet drained).
    std::size_t pending_count() const noexcept { return pending_.size(); }

  private:
    sched_staging_queue          staging_;
    std::vector<scheduled_event> pending_;
};

} // namespace nomos::rt
