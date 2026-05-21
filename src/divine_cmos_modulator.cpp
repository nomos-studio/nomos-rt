// SPDX-License-Identifier: LGPL-2.1-or-later
#include "divine_cmos_modulator.hpp"

#include <nomos/rt/modulator_engine.hpp>

#include <algorithm>
#include <cmath>

namespace nomos::rt {

divine_cmos_modulator::divine_cmos_modulator(const modulator_engine* engine,
                                             std::string clock1_src,
                                             std::string clock2_src)
    : engine_(engine)
    , clock1_src_(std::move(clock1_src))
    , clock2_src_(std::move(clock2_src))
{}

bool divine_cmos_modulator::read_gate(const std::string& src_id,
                                      bool& prev) const noexcept {
    if (engine_ && !src_id.empty()) {
        const auto* out = engine_->last_output(src_id);
        if (out) return out->gate;
    }
    return prev;  // no source — hold last value
}

modulator_output divine_cmos_modulator::tick(double /*beat*/, float tick_rate_hz) {
    // Detect rising edges on clock sources and advance counters.
    {
        const bool gate = read_gate(clock1_src_, clock1_prev_);
        if ((gate && !clock1_prev_) || clock1_pending_) {
            counter1_ = (counter1_ + 1) & 0x0F;
            clock1_pending_ = false;
        }
        clock1_prev_ = gate;
    }
    {
        const bool gate = read_gate(clock2_src_, clock2_prev_);
        if ((gate && !clock2_prev_) || clock2_pending_) {
            counter2_ = (counter2_ + 1) & 0x0F;
            clock2_pending_ = false;
        }
        clock2_prev_ = gate;
    }

    // XOR corresponding bits.
    const uint8_t xor_bits = (counter1_ ^ counter2_) & 0x0F;

    // Weighted MAIN sum, normalised.
    float gain_sum = 0.0f;
    float raw      = 0.0f;
    for (int i = 0; i < 4; ++i) {
        gain_sum += gains_[i];
        if ((xor_bits >> i) & 1u)
            raw += gains_[i];
    }
    const float main = (gain_sum > 0.0f) ? raw / gain_sum : 0.0f;

    // One-pole IIR slew (max 4-second time constant at slew=1).
    const float tc    = slew_ * 4.0f;
    const float alpha = (tick_rate_hz > 0.0f && tc > 0.0f)
                        ? std::exp(-1.0f / (tc * tick_rate_hz))
                        : 0.0f;
    slew_state_ = alpha * slew_state_ + (1.0f - alpha) * main;

    modulator_output out;
    out.cv    = main;
    out.aux   = slew_state_;
    out.gate  = (xor_bits & 0x1u) != 0;
    out.gate2 = (xor_bits & 0x2u) != 0;
    out.state = xor_bits;
    return out;
}

void divine_cmos_modulator::update(std::string_view key, float value) {
    if      (key == "gain0") gains_[0] = std::clamp(value, 0.0f, 5.0f);
    else if (key == "gain1") gains_[1] = std::clamp(value, 0.0f, 5.0f);
    else if (key == "gain2") gains_[2] = std::clamp(value, 0.0f, 5.0f);
    else if (key == "gain3") gains_[3] = std::clamp(value, 0.0f, 5.0f);
    else if (key == "slew")  slew_     = std::clamp(value, 0.0f, 1.0f);
    else if (key == "clock1_tick" && value > 0.5f) clock1_pending_ = true;
    else if (key == "clock2_tick" && value > 0.5f) clock2_pending_ = true;
}

} // namespace nomos::rt
