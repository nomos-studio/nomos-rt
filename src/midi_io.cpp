// SPDX-License-Identifier: GPL-2.0-or-later
#include "midi_io.hpp"

#include <clap/events.h>

#include <cstdio>

namespace nomos::rt {

midi_io::midi_io() = default;
midi_io::~midi_io() {
    close_input();
    close();
}

// ---------------------------------------------------------------------------
// Output
// ---------------------------------------------------------------------------

bool midi_io::open_port(unsigned int index) {
    try {
        const unsigned int count = out_.getPortCount();
        if (index >= count) {
            std::fprintf(stderr, "[midi] output port %u out of range (%u available)\n", index,
                         count);
            return false;
        }
        out_.openPort(index);
        std::fprintf(stderr, "[midi] opened output port [%u] %s\n", index,
                     out_.getPortName(index).c_str());
        return true;
    } catch (const RtMidiError& e) {
        std::fprintf(stderr, "[midi] open_port error: %s\n", e.what());
        return false;
    }
}

bool midi_io::open_port_by_name(const std::string& name) {
    const unsigned int count = out_.getPortCount();
    for (unsigned int i = 0; i < count; ++i) {
        if (out_.getPortName(i).find(name) != std::string::npos)
            return open_port(i);
    }
    std::fprintf(stderr, "[midi] no output port matching '%s'\n", name.c_str());
    return false;
}

void midi_io::close() {
    if (out_.isPortOpen()) {
        out_.closePort();
        std::fprintf(stderr, "[midi] output port closed\n");
    }
}

bool midi_io::is_open() const noexcept {
    return out_.isPortOpen();
}

void midi_io::send(const std::vector<uint8_t>& bytes) {
    if (!is_open())
        return;
    try {
        out_.sendMessage(&bytes);
    } catch (const RtMidiError& e) {
        std::fprintf(stderr, "[midi] send error: %s\n", e.what());
    }
}

void midi_io::note_on(uint8_t channel, uint8_t note, uint8_t velocity) {
    const uint8_t ch = static_cast<uint8_t>((channel - 1) & 0x0F);
    send({static_cast<uint8_t>(0x90 | ch), note, velocity});
}

void midi_io::note_off(uint8_t channel, uint8_t note) {
    const uint8_t ch = static_cast<uint8_t>((channel - 1) & 0x0F);
    send({static_cast<uint8_t>(0x80 | ch), note, 0x00});
}

void midi_io::cc(uint8_t channel, uint8_t cc_num, uint8_t value) {
    const uint8_t ch = static_cast<uint8_t>((channel - 1) & 0x0F);
    send({static_cast<uint8_t>(0xB0 | ch), cc_num, value});
}

void midi_io::pitch_bend(uint8_t channel, uint16_t value14) {
    const uint8_t ch  = static_cast<uint8_t>((channel - 1) & 0x0F);
    const uint8_t lsb = static_cast<uint8_t>(value14 & 0x7F);
    const uint8_t msb = static_cast<uint8_t>((value14 >> 7) & 0x7F);
    send({static_cast<uint8_t>(0xE0 | ch), lsb, msb});
}

void midi_io::channel_pressure(uint8_t channel, uint8_t pressure) {
    const uint8_t ch = static_cast<uint8_t>((channel - 1) & 0x0F);
    send({static_cast<uint8_t>(0xD0 | ch), pressure});
}

void midi_io::realtime(uint8_t byte) {
    send({byte});
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

bool midi_io::open_input_port(unsigned int index, input_event_queue& q) {
    try {
        const unsigned int count = in_.getPortCount();
        if (index >= count) {
            std::fprintf(stderr, "[midi] input port %u out of range (%u available)\n", index,
                         count);
            return false;
        }
        in_queue_ = &q;
        in_.setCallback(midi_callback, this);
        in_.ignoreTypes(false, true, true); // pass notes/CC; ignore sysex/timing/sensing
        in_.openPort(index);
        std::fprintf(stderr, "[midi] opened input port [%u] %s\n", index,
                     in_.getPortName(index).c_str());
        return true;
    } catch (const RtMidiError& e) {
        std::fprintf(stderr, "[midi] open_input_port error: %s\n", e.what());
        in_queue_ = nullptr;
        return false;
    }
}

bool midi_io::open_input_port_by_name(const std::string& name, input_event_queue& q) {
    const unsigned int count = in_.getPortCount();
    for (unsigned int i = 0; i < count; ++i) {
        if (in_.getPortName(i).find(name) != std::string::npos)
            return open_input_port(i, q);
    }
    std::fprintf(stderr, "[midi] no input port matching '%s'\n", name.c_str());
    return false;
}

void midi_io::close_input() {
    if (in_.isPortOpen()) {
        in_.cancelCallback();
        in_.closePort();
        in_queue_ = nullptr;
        std::fprintf(stderr, "[midi] input port closed\n");
    }
}

bool midi_io::input_is_open() const noexcept {
    return in_.isPortOpen();
}

void midi_io::midi_callback(double /*timestamp*/, std::vector<uint8_t>* msg,
                            void* user_data) noexcept {
    if (!msg || msg->empty())
        return;
    auto* self = static_cast<midi_io*>(user_data);
    if (!self->in_queue_)
        return;

    const uint8_t status = (*msg)[0];
    const uint8_t nibble = status & 0xF0;

    // Translate note events to CLAP_EVENT_NOTE_ON / NOTE_OFF so plugins that
    // don't implement CLAP_EVENT_MIDI can still respond to the keyboard.
    if (nibble == 0x90 || nibble == 0x80) {
        const uint8_t vel    = (msg->size() > 2) ? (*msg)[2] : 0;
        const bool    is_off = (nibble == 0x80) || (nibble == 0x90 && vel == 0);

        clap_event_union ev{};
        ev.note.header.size     = sizeof(clap_event_note_t);
        ev.note.header.time     = 0;
        ev.note.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        ev.note.header.type     = is_off ? CLAP_EVENT_NOTE_OFF : CLAP_EVENT_NOTE_ON;
        ev.note.header.flags    = CLAP_EVENT_IS_LIVE;
        ev.note.note_id         = -1;
        ev.note.port_index      = 0;
        ev.note.channel         = static_cast<int16_t>(status & 0x0F);
        ev.note.key             = (msg->size() > 1) ? static_cast<int16_t>((*msg)[1]) : 0;
        ev.note.velocity        = is_off ? 0.0 : static_cast<double>(vel) / 127.0;
        self->in_queue_->push(ev);
        return;
    }

    // Everything else → raw CLAP_EVENT_MIDI.
    clap_event_union ev{};
    ev.midi.header.size     = sizeof(clap_event_midi_t);
    ev.midi.header.time     = 0;
    ev.midi.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    ev.midi.header.type     = CLAP_EVENT_MIDI;
    ev.midi.header.flags    = CLAP_EVENT_IS_LIVE;
    ev.midi.port_index      = 0;
    ev.midi.data[0]         = status;
    ev.midi.data[1]         = (msg->size() > 1) ? (*msg)[1] : 0;
    ev.midi.data[2]         = (msg->size() > 2) ? (*msg)[2] : 0;
    self->in_queue_->push(ev);
}

// ---------------------------------------------------------------------------
// Static utilities
// ---------------------------------------------------------------------------

void midi_io::list_ports() {
    try {
        {
            RtMidiOut          probe;
            const unsigned int n = probe.getPortCount();
            std::fprintf(stderr, "[midi] output ports (%u):\n", n);
            for (unsigned int i = 0; i < n; ++i)
                std::fprintf(stderr, "  [%u] %s\n", i, probe.getPortName(i).c_str());
        }
        {
            RtMidiIn           probe;
            const unsigned int n = probe.getPortCount();
            std::fprintf(stderr, "[midi] input ports (%u):\n", n);
            for (unsigned int i = 0; i < n; ++i)
                std::fprintf(stderr, "  [%u] %s\n", i, probe.getPortName(i).c_str());
        }
    } catch (const RtMidiError& e) {
        std::fprintf(stderr, "[midi] list error: %s\n", e.what());
    }
}

} // namespace nomos::rt
