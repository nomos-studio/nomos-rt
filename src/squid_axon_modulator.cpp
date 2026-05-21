// SPDX-License-Identifier: LGPL-2.1-or-later
#include "squid_axon_modulator.hpp"

#include <nomos/rt/modulator_engine.hpp>

#include <algorithm>

namespace nomos::rt {

squid_axon_modulator::squid_axon_modulator(const modulator_engine* engine,
                                           std::string clock_src,
                                           std::string in1_src,
                                           std::string in2_src,
                                           std::string in3_src)
    : engine_(engine)
    , clock_src_(std::move(clock_src))
    , in1_src_(std::move(in1_src))
    , in2_src_(std::move(in2_src))
    , in3_src_(std::move(in3_src))
{}

float squid_axon_modulator::read_cv_src(const std::string& src,
                                         float fallback) const noexcept {
    if (engine_ && !src.empty()) {
        const auto* o = engine_->last_output(src);
        if (o) return o->cv;
    }
    return fallback;
}

float squid_axon_modulator::nl_feedback(float out4) const noexcept {
    if (nl_fb_ < 1e-4f) return 0.0f;
    // Threshold in normalised [0,1] space: threshold_norm = 1 − nl_fb/4
    // Clamped so nl_fb=4 gives threshold=0 (always active).
    const float threshold = std::max(0.0f, 1.0f - nl_fb_ / 4.0f);
    const float excess    = out4 - threshold;
    if (excess <= 0.0f) return 0.0f;
    return -0.7f * excess * excess;
}

modulator_output squid_axon_modulator::tick(double /*beat*/, float /*tick_rate_hz*/) {
    // Resolve clock edge.
    bool clk = false;
    if (engine_ && !clock_src_.empty()) {
        const auto* o = engine_->last_output(clock_src_);
        if (o) clk = o->gate;
    }
    const bool edge = (clk && !clock_prev_) || clock_pending_;
    clock_prev_    = clk;
    clock_pending_ = false;

    if (edge) {
        const float in1 = read_cv_src(in1_src_, in1_);
        const float in2 = read_cv_src(in2_src_, in2_);
        // IN3: normalled to OUT4 when not patched.
        const float in3 = (!in3_patched_ && in3_src_.empty())
                          ? stages_[3]
                          : read_cv_src(in3_src_, in3_);

        const float nl  = nl_feedback(stages_[3]);
        const float lin = lin_fb_ * stages_[3];

        float mix = in1 + in2 + in3 + nl + lin;
        mix = std::clamp(mix, 0.0f, 1.0f);

        // Shift pipeline: OUT4←OUT3←OUT2←OUT1←mix.
        stages_[3] = stages_[2];
        stages_[2] = stages_[1];
        stages_[1] = stages_[0];
        stages_[0] = mix;

        counter_ = (counter_ + 1) & 3;
    }

    modulator_output out;
    out.cv    = stages_[0];
    out.aux   = stages_[3];
    out.gate  = stages_[0] > 0.5f;
    out.state = static_cast<uint32_t>(counter_);
    for (int i = 0; i < 4; ++i)
        out.outputs[i] = stages_[i];
    return out;
}

void squid_axon_modulator::update(std::string_view key, float value) {
    if      (key == "nl_fb")       nl_fb_  = std::clamp(value, 0.0f, 4.0f);
    else if (key == "lin_fb")      lin_fb_ = std::clamp(value, 0.0f, 1.0f);
    else if (key == "in1")         in1_    = value;
    else if (key == "in2")         in2_    = value;
    else if (key == "in3")         in3_    = value;
    else if (key == "in3_patched") in3_patched_ = (value > 0.5f);
    else if (key == "clock_tick" && value > 0.5f) clock_pending_ = true;
}

} // namespace nomos::rt
