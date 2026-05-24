// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "nomos/rt/abstract_modulator.hpp"
#include "nomos/rt/dsp_block.hpp"

#include <memory>

namespace nomos::rt {

// Wraps an alembic-compiled dsp_block as a nomos-rt block-rate modulator.
//
// I/O contract (matches the named-output convention in alembic defpatch!):
//
//   Faust output channel → modulator_output field
//   0  → cv     (and outputs[0])
//   1  → aux    (and outputs[1])   zero if patch has < 2 outputs
//   2  → gate   (and outputs[2])   float > 0.5 → true; zero if < 3 outputs
//   3  → gate2  (and outputs[3])   float > 0.5 → true; zero if < 4 outputs
//   4+ → outputs[4+]
//
//   state — bitmask: bit i set when outputs[i] > 0.5f
//
// Beat input:
//   The reserved param name "beat" is set to the fractional beat phase
//   (beat − floor(beat), ∈ [0, 1)) before each compute call.  Patches
//   use (param :beat) in defpatch! to access this signal.
//
// Sample rate:
//   init(tick_rate_hz) is called on first tick (lazily, to capture the live
//   rate).  Re-init occurs if tick_rate_hz changes by more than 1 Hz.
class faust_modulator final : public abstract_modulator {
public:
    explicit faust_modulator(std::unique_ptr<dsp_block> dsp);

    modulator_output tick(double beat, float tick_rate_hz) override;
    void update(std::string_view key, float value) override;

private:
    void ensure_init(float tick_rate_hz);

    std::unique_ptr<dsp_block> dsp_;
    float   current_rate_{0.0f};
    bool    initialised_{false};

    static constexpr int kBufSize = modulator_output::kMaxOutputs;
    float   samples_[kBufSize]{};
    float*  ptrs_[kBufSize]{};
};

} // namespace nomos::rt
