// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <nomos/rt/input_event.hpp>

#include <RtMidi.h>

#include <cstdint>
#include <string>
#include <vector>

namespace nomos::rt {

// Thin RAII wrapper around RtMidiOut + RtMidiIn.  Lives only in the executable
// (GPL scope).
//
// Channel arguments are 1-indexed (matching cljseq convention).
// Port-selection by name is a substring match — first matching port wins.
//
// MIDI input: hardware note and MIDI events are translated to clap_event_union
// and pushed to the input_event_queue supplied at open_input_port time.
// Note On with velocity=0 is normalised to Note Off per MIDI 1.0 convention.
class midi_io {
  public:
    midi_io();
    ~midi_io();

    midi_io(const midi_io&)            = delete;
    midi_io& operator=(const midi_io&) = delete;

    // Output
    bool open_port(unsigned int index);
    bool open_port_by_name(const std::string& name);
    void close();
    bool is_open() const noexcept;

    void send(const std::vector<uint8_t>& bytes);
    void note_on(uint8_t channel, uint8_t note, uint8_t velocity);
    void note_off(uint8_t channel, uint8_t note);
    void cc(uint8_t channel, uint8_t cc_num, uint8_t value);
    void pitch_bend(uint8_t channel, uint16_t value14); // 14-bit, 0–16383, centre=8192
    void channel_pressure(uint8_t channel, uint8_t pressure);
    void realtime(uint8_t byte);

    // Input
    bool open_input_port(unsigned int index, input_event_queue& q);
    bool open_input_port_by_name(const std::string& name, input_event_queue& q);
    void close_input();
    bool input_is_open() const noexcept;

    static void list_ports();

  private:
    static void midi_callback(double timestamp, std::vector<uint8_t>* msg,
                              void* user_data) noexcept;

    RtMidiOut          out_;
    RtMidiIn           in_;
    input_event_queue* in_queue_{nullptr};
};

} // namespace nomos::rt
