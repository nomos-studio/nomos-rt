// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <nomos/rt/spsc_queue.hpp>

#include <clap/events.h>

#include <cstddef>

namespace nomos::rt {

// Union covering all CLAP input event types handled by kairos.
// The header member is always valid — inspect header.type before casting.
union clap_event_union {
    clap_event_header_t header;
    clap_event_note_t   note;
    clap_event_midi_t   midi;
};

constexpr std::size_t in_queue_capacity = 256;
using input_event_queue                 = spsc_queue<clap_event_union, in_queue_capacity>;

} // namespace nomos::rt
