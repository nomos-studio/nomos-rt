// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "midi_out_event.hpp"

#include <nomos/rt/spsc_queue.hpp>

#include <clap/events.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace nomos::rt {

constexpr std::size_t midi_queue_capacity = 512;
using midi_event_queue                    = spsc_queue<midi_event_block, midi_queue_capacity>;

// Implements clap_output_events_t.  Translates CLAP events into midi_out_event
// and accumulates them in a fixed array (no allocation).
//
// Usage per block:
//   proc.out_events = collector.output_events();
//   plugin->process(plugin, &proc);
//   collector.flush_to(queue);
//   collector.reset();
class event_collector {
  public:
    static constexpr std::size_t max_events = 128;

    event_collector() noexcept;

    void reset() noexcept { count_ = 0; }

    const clap_output_events_t* output_events() const noexcept { return &vtable_; }

    std::size_t count() const noexcept { return count_; }

    // Pack events into cache-line-sized midi_event_blocks and push into queue.
    // Only pushes blocks with count > 0.  Returns the number of blocks pushed.
    std::size_t flush_to(midi_event_queue& queue) noexcept;

  private:
    static bool CLAP_ABI try_push_impl(const clap_output_events_t* list,
                                       const clap_event_header_t*  hdr) noexcept;

    clap_output_events_t                   vtable_;
    std::array<midi_out_event, max_events> events_;
    std::size_t                            count_{0};
};

} // namespace nomos::rt
