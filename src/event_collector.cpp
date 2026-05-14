// SPDX-License-Identifier: GPL-2.0-or-later
#include "event_collector.hpp"

#include <clap/events.h>

#include <algorithm>
#include <cstdint>

namespace nomos::rt {

namespace {

    // Returns the expected byte length of a MIDI message from its status byte.
    uint8_t midi_message_size(uint8_t status) noexcept {
        if (status >= 0xF8)
            return 1; // realtime single-byte
        if (status >= 0xF0)
            return 3; // system common — simplified; sysex handled separately
        switch (status & 0xF0) {
        case 0xC0:
        case 0xD0:
            return 2; // program change, channel pressure
        default:
            return 3; // note, CC, pitch bend, key pressure
        }
    }

} // namespace

event_collector::event_collector() noexcept {
    vtable_.ctx      = this;
    vtable_.try_push = &event_collector::try_push_impl;
}

bool CLAP_ABI event_collector::try_push_impl(const clap_output_events_t* list,
                                             const clap_event_header_t*  hdr) noexcept {
    auto* self = static_cast<event_collector*>(list->ctx);
    if (self->count_ >= max_events)
        return false; // collector full — drop and signal back-pressure

    midi_out_event e{};
    e.sample_offset = static_cast<uint16_t>(std::min<uint32_t>(hdr->time, 0xFFFFu));
    e.port          = 0;

    switch (hdr->type) {
    case CLAP_EVENT_NOTE_ON: {
        const auto* n = reinterpret_cast<const clap_event_note_t*>(hdr);
        if (n->channel < 0 || n->key < 0)
            return true; // accept but discard all-channels / all-keys wildcards
        const auto ch  = static_cast<uint8_t>(n->channel & 0x0F);
        const auto key = static_cast<uint8_t>(n->key & 0x7F);
        const auto vel =
            static_cast<uint8_t>(std::clamp(static_cast<int>(n->velocity * 127.0), 0, 127));
        e.data[0] = static_cast<uint8_t>(0x90 | ch);
        e.data[1] = key;
        e.data[2] = vel;
        e.size    = 3;
        break;
    }
    case CLAP_EVENT_NOTE_OFF: {
        const auto* n = reinterpret_cast<const clap_event_note_t*>(hdr);
        if (n->channel < 0 || n->key < 0)
            return true;
        const auto ch  = static_cast<uint8_t>(n->channel & 0x0F);
        const auto key = static_cast<uint8_t>(n->key & 0x7F);
        const auto vel =
            static_cast<uint8_t>(std::clamp(static_cast<int>(n->velocity * 127.0), 0, 127));
        e.data[0] = static_cast<uint8_t>(0x80 | ch);
        e.data[1] = key;
        e.data[2] = vel;
        e.size    = 3;
        break;
    }
    case CLAP_EVENT_MIDI: {
        const auto* m = reinterpret_cast<const clap_event_midi_t*>(hdr);
        e.port        = static_cast<uint8_t>(m->port_index & 0xFF);
        e.data[0]     = m->data[0];
        e.data[1]     = m->data[1];
        e.data[2]     = m->data[2];
        e.size        = midi_message_size(m->data[0]);
        break;
    }
    default:
        return true; // accepted; not translated (param changes, expressions, etc.)
    }

    if (e.size > 0)
        self->events_[self->count_++] = e;
    return true;
}

std::size_t event_collector::flush_to(midi_event_queue& queue) noexcept {
    std::size_t pushed = 0;
    std::size_t i      = 0;
    while (i < count_) {
        midi_event_block block{};
        while (block.count < 7 && i < count_)
            block.events[block.count++] = events_[i++];
        queue.push(block);
        ++pushed;
    }
    return pushed;
}

} // namespace nomos::rt
