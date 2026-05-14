// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "abstract_modulator.hpp"

#include "tides/generator.h"

#include <cstdint>
#include <string_view>

namespace nomos::rt {

// Tides-inspired function generator / LFO.
//
// Wraps the tides::Generator in LOOPING + RANGE_MEDIUM mode.  Driven as an LFO
// at the event-loop block rate; each tick() call consumes one "virtual sample"
// from the generator's output buffer.
//
// Pitch is derived from the requested rate and the actual tick rate so the LFO
// frequency is independent of buffer size.
//
// Parameters (via update()):
//   "rate"       — LFO rate in Hz              (float, default 1.0)
//   "shape"      — waveform shape [-1..1]       (float, default 0.0)
//   "slope"      — attack/decay balance [-1..1] (float, default 0.0)
//   "smoothness" — filter / fold amount [-1..1] (float, default 0.0)
//   "depth"      — output scale [0..1]          (float, default 1.0)
//   "bipolar"    — 1.0 = bipolar, 0.0 = unipolar (float, default 1.0)
class slope_modulator final : public abstract_modulator {
public:
    slope_modulator();

    float tick(double beat, float tick_rate_hz) override;
    void  update(std::string_view key, float value) override;

private:
    void apply_rate(float tick_rate_hz);

    tides::Generator gen_;

    // Cached parameters.
    float    rate_hz_{1.0f};
    float    shape_{0.0f};
    float    slope_{0.0f};
    float    smoothness_{0.0f};
    float    depth_{1.0f};
    bool     bipolar_{true};

    float    last_tick_rate_{0.0f}; // detect rate changes
};

} // namespace nomos::rt
