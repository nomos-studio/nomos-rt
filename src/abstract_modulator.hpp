// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <string_view>

namespace nomos::rt {

// Base interface for all RT modulators.
//
// Modulators are autonomous: they run on every tick of the event loop without
// further IPC.  The tick() method returns a normalised float output that the
// modulator_engine routes to its registered consumer (CLAP param mod, MIDI CC,
// or discard).
//
// Threading: all methods are called from the event thread.  Control-thread
// updates arrive via modulator_engine which holds a mutex around update().
class abstract_modulator {
public:
    virtual ~abstract_modulator() = default;

    // Advance the modulator by one block and return the current output.
    //   beat         — current Link beat position (for tempo-sync use)
    //   tick_rate_hz — event-loop tick rate in Hz (= sample_rate / buffer_frames)
    // Return value is normalised to [-1, 1] (bipolar) or [0, 1] (unipolar).
    virtual float tick(double beat, float tick_rate_hz) = 0;

    // Update a named parameter.  Parameter names are modulator-type specific.
    // Called from the event thread under modulator_engine's mutex.
    virtual void update(std::string_view key, float value) = 0;
};

} // namespace nomos::rt
