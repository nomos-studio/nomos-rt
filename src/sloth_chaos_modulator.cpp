// SPDX-License-Identifier: LGPL-2.1-or-later
#include "sloth_chaos_modulator.hpp"

#include <nomos/rt/modulator_engine.hpp>

#include <algorithm>

namespace nomos::rt {

constexpr float sloth_chaos_modulator::kTimeDilation[3];

// ---------------------------------------------------------------------------
// State — one Sloth circuit
// ---------------------------------------------------------------------------

void sloth_chaos_modulator::State::step(float h, float K,
                                        float z_bias, float cv_in) noexcept {
    // Update comparator instantaneously (threshold shifts with knob via z_bias).
    z = (x + cv_in > z_bias) ? kZHigh : kZLow;

    const float dx = (y - x * K) * kAlpha;
    const float dw = (z - x)     * kBeta;
    const float dy = -w           * kGamma;

    x += dx * h;
    w += dw * h;
    y += dy * h;
}

float sloth_chaos_modulator::State::x_norm() const noexcept {
    return std::clamp((x + kVRange) / (2.0f * kVRange), 0.0f, 1.0f);
}

float sloth_chaos_modulator::State::y_norm() const noexcept {
    return std::clamp((y + kVRange) / (2.0f * kVRange), 0.0f, 1.0f);
}

float sloth_chaos_modulator::State::w_norm() const noexcept {
    return std::clamp((w + kVRange) / (2.0f * kVRange), 0.0f, 1.0f);
}

// ---------------------------------------------------------------------------
// Modulator
// ---------------------------------------------------------------------------

sloth_chaos_modulator::sloth_chaos_modulator(variant v,
                                             const modulator_engine* engine,
                                             std::string cv_src)
    : variant_(v)
    , engine_(engine)
    , cv_src_(std::move(cv_src))
{}

modulator_output sloth_chaos_modulator::tick(double /*beat*/, float tick_rate_hz) {
    // Resolve CV nudge input.
    float cv_in = cv_in_;
    if (engine_ && !cv_src_.empty()) {
        const auto* o = engine_->last_output(cv_src_);
        if (o) cv_in = o->cv;
    }

    const float K      = 1.0f + (knob_ - 0.5f) * 0.4f;   // [0.8, 1.2]
    const float z_bias = (knob_ - 0.5f) * 4.0f;           // ±2 V lobe bias

    // Integrate — 4 Euler sub-steps per tick.
    const int v_idx      = (variant_ == variant::triple) ? 0 :
                           (variant_ == variant::torpor)  ? 0 :
                           (variant_ == variant::apathy)  ? 1 : 2;

    if (variant_ != variant::triple) {
        const float eff_dt = (tick_rate_hz > 0.0f)
                             ? kTimeDilation[v_idx] / tick_rate_hz
                             : 0.0f;
        const float h = eff_dt / 4.0f;
        for (int i = 0; i < 4; ++i)
            states_[v_idx].step(h, K, z_bias, cv_in);
    } else {
        for (int vi = 0; vi < 3; ++vi) {
            const float eff_dt = (tick_rate_hz > 0.0f)
                                 ? kTimeDilation[vi] / tick_rate_hz
                                 : 0.0f;
            const float h = eff_dt / 4.0f;
            for (int i = 0; i < 4; ++i)
                states_[vi].step(h, K, z_bias, cv_in);
        }
    }

    modulator_output out;

    if (variant_ != variant::triple) {
        State& s = states_[v_idx];
        const bool lobe_now = s.z > 0.0f;
        const bool transition = (lobe_now != s.z_prev_positive);
        s.z_prev_positive = lobe_now;

        out.cv    = s.x_norm();
        out.aux   = s.y_norm();
        out.gate  = lobe_now;
        out.gate2 = transition;
        out.state = lobe_now ? 1u : 0u;

        out.outputs[0] = s.x_norm();
        out.outputs[1] = s.y_norm();
        out.outputs[2] = s.w_norm();
        // z_norm: map kZLow→0.0, kZHigh→1.0
        out.outputs[3] = (s.z > 0.0f) ? 1.0f : 0.0f;
    } else {
        // Triple: fill outputs[0..6], cv = torpor x, aux = combined.
        float z_vals[3];
        for (int vi = 0; vi < 3; ++vi) {
            State& s = states_[vi];
            s.z_prev_positive = (s.z > 0.0f);
            out.outputs[vi * 2]     = s.x_norm();
            out.outputs[vi * 2 + 1] = s.y_norm();
            z_vals[vi] = s.z;
        }
        // combined = 0.7 × (torpor_z − (apathy_z + inertia_z))
        const float combined_v = 0.7f * (z_vals[0] - (z_vals[1] + z_vals[2]));
        // Approximate normalisation: combined range ≈ ±16 V → [0,1]
        out.outputs[6] = std::clamp((combined_v + 16.0f) / 32.0f, 0.0f, 1.0f);
        out.cv         = out.outputs[0];
        out.aux        = out.outputs[6];
        out.gate       = states_[0].z > 0.0f;
        out.state      = (states_[0].z > 0.0f ? 1u : 0u)
                       | (states_[1].z > 0.0f ? 2u : 0u)
                       | (states_[2].z > 0.0f ? 4u : 0u);
    }

    return out;
}

void sloth_chaos_modulator::update(std::string_view key, float value) {
    if      (key == "knob")   knob_  = std::clamp(value, 0.0f, 1.0f);
    else if (key == "cv_in")  cv_in_ = value;
}

} // namespace nomos::rt
