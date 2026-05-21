// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <nomos/rt/abstract_modulator.hpp>

#include <string>
#include <string_view>

namespace nomos::rt {

class modulator_engine;

// Squid Axon 4-stage analog pipeline with nonlinear self-feedback.
//
// Emulation of the NLC Squid Axon (Andrew Fitch).  VCV Rack port by
// Michael Hetrick (OSS).
//
// A stage counter cycles 0→3 on each clock edge.  On stage 0 the module
// reads the input mix plus both feedback signals and writes a new value to
// OUT1.  On stages 1–3 it shifts the previous output forward one stage.
// Each output therefore holds for four clock ticks; the same value that
// enters OUT1 reaches OUT4 exactly three ticks later.
//
// Nonlinear feedback path: a threshold function of OUT4 (quadratic above
// the threshold, zero below), multiplied by −0.7 and added to the mix.
// Acts as a stabiliser: large OUT4 injects an opposing signal.
//
// Linear feedback path: OUT4 (or IN3 if patched) scaled by lin_fb, added
// directly to the mix.  Creates repeating contours.
//
// All values are normalised to [0, 1] (representing ±12 V from hardware).
//
// Parameters (via update()):
//   "nl_fb"       — nonlinear feedback drive [0, 4]  (default 0)
//   "lin_fb"      — linear feedback amount   [0, 1]  (default 0)
//   "clock_tick"  — >0.5 arms one clock edge (one-shot)
//   "in1" / "in2" — direct CV inputs to mixer [0, 1] (default 0)
//   "in3"         — third mixer input (default: normalled to OUT4 internally)
//   "in3_patched" — >0.5 means in3 is an explicit external signal
//
// Outputs (modulator_output):
//   .cv         — OUT1 normalised [0, 1]
//   .aux        — OUT4 normalised [0, 1]
//   .gate       — OUT1 > 0.5
//   .state      — stage counter (0–3)
//   .outputs[0..3] = OUT1..OUT4 normalised
class squid_axon_modulator final : public abstract_modulator {
public:
    explicit squid_axon_modulator(const modulator_engine* engine   = nullptr,
                                  std::string             clock_src = {},
                                  std::string             in1_src   = {},
                                  std::string             in2_src   = {},
                                  std::string             in3_src   = {});

    modulator_output tick(double beat, float tick_rate_hz) override;
    void             update(std::string_view key, float value) override;

private:
    float read_cv_src(const std::string& src, float fallback) const noexcept;
    float nl_feedback(float out4) const noexcept;

    const modulator_engine* engine_;
    std::string             clock_src_, in1_src_, in2_src_, in3_src_;

    float stages_[4]{};
    int   counter_{0};

    float nl_fb_{0.0f};
    float lin_fb_{0.0f};
    float in1_{0.0f}, in2_{0.0f}, in3_{0.0f};
    bool  in3_patched_{false};

    bool  clock_prev_{false};
    bool  clock_pending_{false};
};

} // namespace nomos::rt
