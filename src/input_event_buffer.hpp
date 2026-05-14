// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <nomos/rt/input_event.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace nomos::rt {

// Implements clap_input_events_t.  Drains one or more input_event_queues into a
// fixed array and presents them to CLAP plugins via the standard vtable.
//
// Usage per block:
//   buffer.clear();
//   buffer.drain(ipc_queue);
//   buffer.drain(hw_midi_queue);
//   buffer.drain(osc_queue);
//   proc.in_events = buffer.input_events();
class input_event_buffer {
  public:
    static constexpr std::size_t max_events = 64;

    input_event_buffer() noexcept;

    void        clear() noexcept { count_ = 0; }
    std::size_t count() const noexcept { return count_; }

    void drain(input_event_queue& q) noexcept;

    const clap_input_events_t* input_events() const noexcept { return &vtable_; }

  private:
    static uint32_t CLAP_ABI                   size_impl(const clap_input_events_t* list) noexcept;
    static const clap_event_header_t* CLAP_ABI get_impl(const clap_input_events_t* list,
                                                        uint32_t                   index) noexcept;

    clap_input_events_t                      vtable_;
    std::array<clap_event_union, max_events> events_;
    std::size_t                              count_{0};
};

} // namespace nomos::rt
