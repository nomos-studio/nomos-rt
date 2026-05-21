// SPDX-License-Identifier: LGPL-2.1-or-later
#include "bools_ring_modulator.hpp"

#include <nomos/rt/modulator_engine.hpp>

#include <algorithm>
#include <cmath>

namespace nomos::rt {

constexpr float bools_ring_modulator::kStepWeights[4];
constexpr float bools_ring_modulator::kStepMax;

bools_ring_modulator::bools_ring_modulator(mode m,
                                           const modulator_engine* engine,
                                           std::string src0,
                                           std::string src1,
                                           std::string src2,
                                           std::string src3,
                                           std::string sample_src)
    : mode_(m)
    , engine_(engine)
    , src_{std::move(src0), std::move(src1), std::move(src2), std::move(src3)}
    , sample_src_(std::move(sample_src))
{}

bool bools_ring_modulator::read_gate(const std::string& src_id,
                                     float fallback) const noexcept {
    if (engine_ && !src_id.empty()) {
        const auto* out = engine_->last_output(src_id);
        if (out) return out->gate;
    }
    return fallback > 0.5f;
}

bool bools_ring_modulator::apply_logic(bool a, bool b) const noexcept {
    switch (mode_) {
        case mode::xor_mode:  return a ^ b;
        case mode::or_mode:   return a || b;
        case mode::and_mode:  return a && b;
        case mode::nor_mode:  return !(a || b);
        case mode::nand_mode: return !(a && b);
        case mode::xnor_mode: return !(a ^ b);
    }
    return false;
}

modulator_output bools_ring_modulator::tick(double /*beat*/, float tick_rate_hz) {
    // Resolve inputs.
    bool ins[4];
    for (int i = 0; i < 4; ++i)
        ins[i] = read_gate(src_[i], inputs_[i]);

    // Compute ring outputs.
    uint8_t bitmap = 0;
    for (int i = 0; i < 4; ++i)
        if (apply_logic(ins[i], ins[(i + 1) & 3]))
            bitmap |= (1u << i);

    // Sample / latch mode.
    if (sampled_) {
        const bool sgate = !sample_src_.empty()
                           ? read_gate(sample_src_, 0.0f)
                           : false;
        const bool edge = (sgate && !sample_prev_) || sample_pending_;
        sample_prev_    = sgate;
        sample_pending_ = false;
        if (edge) latched_out_ = bitmap;
        bitmap = latched_out_;
    }

    // STEP DAC.
    float step = 0.0f;
    for (int i = 0; i < 4; ++i)
        if ((bitmap >> i) & 1u)
            step += kStepWeights[i];
    const float step_norm = step / kStepMax;

    // One-pole IIR slew (max 4-second time constant).
    const float tc    = slew_ * 4.0f;
    const float alpha = (tick_rate_hz > 0.0f && tc > 0.0f)
                        ? std::exp(-1.0f / (tc * tick_rate_hz))
                        : 0.0f;
    slew_state_ = alpha * slew_state_ + (1.0f - alpha) * step_norm;

    modulator_output out;
    out.cv    = step_norm;
    out.aux   = slew_state_;
    out.gate  = (bitmap & 0x1u) != 0;
    out.gate2 = (bitmap & 0x2u) != 0;
    out.state = bitmap;
    return out;
}

void bools_ring_modulator::update(std::string_view key, float value) {
    if      (key == "in0")   inputs_[0] = value;
    else if (key == "in1")   inputs_[1] = value;
    else if (key == "in2")   inputs_[2] = value;
    else if (key == "in3")   inputs_[3] = value;
    else if (key == "slew")  slew_      = std::clamp(value, 0.0f, 1.0f);
    else if (key == "sampled") sampled_ = (value > 0.5f);
    else if (key == "sample_tick" && value > 0.5f) sample_pending_ = true;
}

} // namespace nomos::rt
