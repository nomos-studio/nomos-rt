// SPDX-License-Identifier: LGPL-2.1-or-later
#include "cipher_modulator.hpp"

#include <nomos/rt/modulator_engine.hpp>

namespace nomos::rt {

constexpr float cipher_modulator::kWeights[4];
constexpr float cipher_modulator::kWeightSum;
constexpr int   cipher_modulator::kCVBits[4][4];

cipher_modulator::cipher_modulator(const modulator_engine* engine,
                                   std::string clock_src,
                                   std::string data1_src,
                                   std::string data2_src,
                                   std::string strobe_src)
    : engine_(engine)
    , clock_src_(std::move(clock_src))
    , data1_src_(std::move(data1_src))
    , data2_src_(std::move(data2_src))
    , strobe_src_(std::move(strobe_src))
{}

bool cipher_modulator::read_gate_src(const std::string& src_id) const noexcept {
    if (engine_ && !src_id.empty()) {
        const auto* out = engine_->last_output(src_id);
        if (out) return out->gate;
    }
    return false;
}

bool cipher_modulator::read_gate_level(const std::string& src_id,
                                       float fallback) const noexcept {
    if (engine_ && !src_id.empty()) {
        const auto* out = engine_->last_output(src_id);
        if (out) return out->gate;
    }
    return fallback > 0.5f;
}

float cipher_modulator::project_cv(int cv_idx) const noexcept {
    float sum = 0.0f;
    for (int w = 0; w < 4; ++w)
        if ((output_reg_ >> kCVBits[cv_idx][w]) & 1u)
            sum += kWeights[w];
    return sum / kWeightSum;
}

modulator_output cipher_modulator::tick(double /*beat*/, float /*tick_rate_hz*/) {
    // Resolve clock rising edge.
    const bool clk = !clock_src_.empty() ? read_gate_src(clock_src_) : false;
    const bool clock_edge = (clk && !clock_prev_) || clock_pending_;
    clock_prev_   = clk;
    clock_pending_ = false;

    serial_out_ = false;
    if (clock_edge) {
        // Determine new LSB.
        const bool d1 = read_gate_level(data1_src_, data1_);
        bool bit_in;
        if (!data2_src_.empty() || data2_ >= 0.0f) {
            // Open mode: data1 OR data2.
            const bool d2 = !data2_src_.empty() ? read_gate_src(data2_src_)
                                                 : (data2_ > 0.5f);
            bit_in = d1 || d2;
        } else {
            // XOR feedback mode: (bit7 XOR bit0) OR data1.
            const bool fb = ((inner_reg_ >> 7) & 1u) ^ (inner_reg_ & 1u);
            bit_in = fb || d1;
        }

        serial_out_ = (inner_reg_ >> 7) & 1u;
        inner_reg_  = static_cast<uint8_t>((inner_reg_ << 1) | (bit_in ? 1 : 0));
    }

    // STROBE: freeze output register while strobe is high.
    const bool strobe_active = read_gate_level(strobe_src_, strobe_);
    if (!strobe_active)
        output_reg_ = inner_reg_;

    modulator_output out;
    out.cv    = project_cv(0);
    out.aux   = project_cv(1);
    out.gate  = serial_out_;
    out.gate2 = strobe_active;
    out.state = output_reg_;
    for (int i = 0; i < 4; ++i)
        out.outputs[i] = project_cv(i);
    return out;
}

void cipher_modulator::update(std::string_view key, float value) {
    if      (key == "data1")        data1_  = value;
    else if (key == "data2")        data2_  = value;
    else if (key == "strobe")       strobe_ = value;
    else if (key == "clock_tick" && value > 0.5f) clock_pending_ = true;
    else if (key == "data2_enable") data2_  = (value > 0.5f) ? 0.0f : -1.0f;
}

} // namespace nomos::rt
