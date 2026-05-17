// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <nomos/rt/abstract_modulator.hpp>

#include <cstdint>
#include <string_view>

namespace nomos::rt {

// Fractal (fBm) modulation source.
//
// Sums N oscillators at geometrically-spaced frequencies with geometrically-
// decreasing amplitudes, producing a self-similar CV signal whose character
// spans multiple time scales simultaneously.
//
//   freq[i]  = base_rate × lacunarity^i
//   amp[i]   = persistence^i
//   output   = Σ amp[i] × generator[i](phase[i])  /  Σ amp[i]
//
// persistence=0.5, lacunarity=2.0 (defaults) → 1/f (pink) spectral character.
// octaves=1 reduces to a single oscillator.
//
// Generator shapes (selected via "shape"):
//   0 — smooth  : sine per octave
//   1 — angular : triangle per octave
//   2 — stepped : sample-and-hold; new random value on each phase wrap
//
// Gate output fires for one tick when output crosses threshold from below
// (rising edge).  Track prev_output_ against the unscaled signal so depth
// does not affect gate timing.
//
// Parameters (via update()):
//   "base_rate"   — Hz of the slowest octave    [0.001, 100]  (default 0.1)
//   "octaves"     — octaves summed              [1, 8]        (default 4)
//   "lacunarity"  — frequency ratio per octave  [1.1, 8.0]    (default 2.0)
//   "persistence" — amplitude ratio per octave  [0.01, 2.0]   (default 0.5)
//   "shape"       — 0=smooth / 1=angular / 2=stepped           (default 0)
//   "threshold"   — gate crossing level         [-1, 1]        (default 0.2)
//   "depth"       — output scale                [0, 1]         (default 1.0)
class fractal_modulator final : public abstract_modulator {
public:
    explicit fractal_modulator();

    modulator_output tick(double beat, float tick_rate_hz) override;
    void             update(std::string_view key, float value) override;

    static constexpr int kMaxOctaves = 8;

private:

    float sample(int i)       const noexcept;
    float next_rand(int i)    noexcept;

    float base_rate_{0.1f};
    float lacunarity_{2.0f};
    float persistence_{0.5f};
    float threshold_{0.2f};
    float depth_{1.0f};
    int   octaves_{4};
    int   shape_{0};

    float    phase_[kMaxOctaves]{};
    float    held_[kMaxOctaves]{};
    uint32_t rand_[kMaxOctaves]{};

    float prev_output_{0.0f};
    bool  gate_out_{false};
};

} // namespace nomos::rt
