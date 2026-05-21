// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <cstdint>
#include <string_view>

namespace nomos::rt {

// All outputs from a single modulator tick, bundled in one value.
//
//   cv      — primary CV, depth-scaled, normalised to [-1,1] or [0,1]
//   aux     — secondary CV (slew output, passthrough, etc.)
//   gate    — primary gate: gate_out (stochastic/fractal/shift_register), eor (slew)
//   gate2   — secondary gate: eoc (slew only)
//   state   — packed integer: shift-register word, channel bitmap, address, etc.
//   outputs — extended named outputs for multi-channel modulators:
//               statues:  outputs[0..7] = 8 held slot values
//               cipher:   outputs[0..3] = cv1..cv4 weighted projections
//               genie:    outputs[0..2] = n1/n2/n3 neuron values  (future)
//               sloth:    outputs[0..2] = x/y/z attractor axes    (future)
//             Unused slots are zero-initialised; existing single-output
//             modulators leave outputs[] untouched.
struct modulator_output {
    float    cv{0.0f};
    float    aux{0.0f};
    bool     gate{false};
    bool     gate2{false};
    uint32_t state{0};

    static constexpr int kMaxOutputs = 8;
    float outputs[kMaxOutputs]{};
};

// Base interface for all RT modulators.
//
// Modulators are autonomous: they run on every tick of the event loop without
// further IPC.  tick() returns a modulator_output that the modulator_engine
// routes to its registered consumer (CLAP param mod, MIDI CC, or discard).
//
// Threading: tick() is called from the event thread.  update() is called from
// the control thread under modulator_engine's write mutex; there is a benign
// relaxed race on the float parameters between the two threads — musically
// negligible at control rate.
class abstract_modulator {
public:
    virtual ~abstract_modulator() = default;

    // Advance the modulator by one block and return all outputs.
    //   beat         — current Link beat position (for tempo-sync use)
    //   tick_rate_hz — event-loop tick rate in Hz (= sample_rate / buffer_frames)
    virtual modulator_output tick(double beat, float tick_rate_hz) = 0;

    // Update a named parameter.  Parameter names are modulator-type specific.
    virtual void update(std::string_view key, float value) = 0;
};

} // namespace nomos::rt
