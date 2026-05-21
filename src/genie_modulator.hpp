// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <nomos/rt/abstract_modulator.hpp>

#include <string>
#include <string_view>

namespace nomos::rt {

class modulator_engine;

// GENiE nonlinear neuron ring oscillator / signal processor.
//
// Emulation of the NLC GENiE (Andrew Fitch).  VCV Rack port by
// Michael Hetrick (OSS).
//
// N neurons (2–8, default 3) are wired in a ring: N_i's output feeds
// N_{i+1}'s input (with wrap-around), scaled by each neuron's in_gain.
//
// Each NLC Neuron:
//   output = clamp(input + sense, 0, 10) − comp
// where comp = +response when (input+sense ≥ 0), else −response.
// The comparator kick of 2×response at threshold is the nonlinearity that
// enables oscillation.
//
// Diff-Rect: (N1 + N3) − N2, split by sign into pos (gate) and neg (gate2).
// This is fixed to neurons 0, 1, 2 regardless of N.
//
// Dynamics at block rate (375 Hz):
//   in_gain 0.0–0.3 : convergent — nonlinear signal conditioner
//   in_gain 0.4–0.7 : periodic oscillation at LFO rate
//   in_gain 0.8–1.0 : chaotic
//
// Parameters (via update() or constructor):
//   "sense0".."sense7"      — per-neuron threshold bias [0, 5]  (default 2.5)
//   "response0".."response7"— per-neuron comparator jump [1, 10] (default 3.0)
//   "gain0".."gain7"        — per-neuron ring in-gain  [0, 1]   (default 0.6)
//   "in0".."in7"            — per-neuron direct CV input (optional external)
//
// External inputs override the ring normal when their src_ids are non-empty.
//
// Outputs (modulator_output):
//   .cv         — N1 normalised to [0, 1]
//   .aux        — diff-rect positive half, normalised [0, 1]
//   .gate       — diff-rect > 0
//   .gate2      — diff-rect < 0
//   .state      — N-bit bitmap: bit i = neuron i output > 0
//   .outputs[i] — neuron i normalised [0, 1] for i in 0..N-1
class genie_modulator final : public abstract_modulator {
public:
    static constexpr int kMaxN = 8;

    explicit genie_modulator(int                     n      = 3,
                             const modulator_engine* engine = nullptr,
                             std::string*            in_src = nullptr);

    modulator_output tick(double beat, float tick_rate_hz) override;
    void             update(std::string_view key, float value) override;

private:
    static float neuron(float input, float sense, float response) noexcept;
    float        neuron_norm(float raw, int i) const noexcept;
    float        read_cv_src(const std::string& src, float fallback) const noexcept;

    const modulator_engine* engine_;
    int                     n_;

    std::string in_src_[kMaxN];

    float sense_[kMaxN];
    float response_[kMaxN];
    float gain_[kMaxN];
    float ext_in_[kMaxN]{};

    // Per-neuron previous output (iterates within a tick for one-shot solution)
    float state_[kMaxN]{};
};

} // namespace nomos::rt
