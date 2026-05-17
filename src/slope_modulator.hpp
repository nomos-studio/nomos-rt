// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "abstract_modulator.hpp"

#include <string_view>

namespace nomos::rt {

// Parametric waveform LFO / function generator.
//
// Three continuous axes describe the shape of any modulation output, spanning
// the full space of useful envelope and LFO shapes without discrete type selection.
// Inspired by the Tides shape/slope/smoothness surface; implemented as a pure
// first-principles phase-accumulator + waveform generator at control rate.
//
// Shape axis ([-1, 1]):
//   -1 — triangle (linear rise and fall)
//    0 — sine (smooth cosine arch)
//   +1 — peaked / exponential (narrower peak, steeper sides)
//
// Slope axis ([-1, 1]):
//   -1 — very fast rise, long decay (percussive)
//    0 — symmetric attack and decay
//   +1 — long attack, fast decay (swell)
//
// Smoothness axis ([-1, 1]):
//   < 0 — one-pole low-pass filter (rounds edges, reduces harmonics)
//    0  — neutral passthrough
//   > 0 — wavefold (sharpens, adds harmonics)
//
// Bipolar mode: output in [-1, 1].  Unipolar mode: output in [0, 1].
//
// Parameters (via update()):
//   "rate"       — LFO rate in Hz            [0.001, 100]   (default 1.0)
//   "shape"      — waveform shape            [-1, 1]        (default 0.0)
//   "slope"      — attack/decay balance      [-1, 1]        (default 0.0)
//   "smoothness" — filter / fold amount      [-1, 1]        (default 0.0)
//   "depth"      — output scale              [0, 1]         (default 1.0)
//   "bipolar"    — 1.0 = bipolar, 0.0 = unipolar            (default 1.0)
class slope_modulator final : public abstract_modulator {
public:
    explicit slope_modulator() = default;

    modulator_output tick(double beat, float tick_rate_hz) override;
    void             update(std::string_view key, float value) override;

private:
    float rate_{1.0f};
    float shape_{0.0f};
    float slope_{0.0f};
    float smoothness_{0.0f};
    float depth_{1.0f};
    bool  bipolar_{true};

    float phase_{0.0f};
    float smooth_state_{0.5f};
    float last_output_{0.0f};
};

} // namespace nomos::rt
