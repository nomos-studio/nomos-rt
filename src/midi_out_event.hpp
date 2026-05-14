// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

namespace nomos::rt {

// One translated MIDI output event from a CLAP output event list.
// 8 bytes, naturally aligned to uint16_t.
struct midi_out_event {
    uint16_t sample_offset; // frame offset within the current block
    uint8_t  port;          // MIDI output port index
    uint8_t  size;          // bytes used in data[] (1–3); 0 = empty
    uint8_t  data[3];       // MIDI status + data bytes
    uint8_t  _pad;
};
static_assert(sizeof(midi_out_event) == 8);

// One cache-line-sized batch: up to 7 events + a count byte.
// Written by the process thread, read by the MIDI dispatch thread via
// spsc_queue<midi_event_block, N>.
struct alignas(64) midi_event_block {
    uint8_t        count;     //  1 byte  — valid events in events[]
    uint8_t        _pad[7];   //  7 bytes — reserved
    midi_out_event events[7]; // 56 bytes — 7 × 8
};
static_assert(sizeof(midi_event_block) == 64);
static_assert(alignof(midi_event_block) == 64);

} // namespace nomos::rt
