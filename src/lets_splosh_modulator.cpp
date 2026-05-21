// SPDX-License-Identifier: LGPL-2.1-or-later
#include "lets_splosh_modulator.hpp"

#include <nomos/rt/modulator_engine.hpp>

#include <algorithm>

namespace nomos::rt {

lets_splosh_modulator::lets_splosh_modulator(const modulator_engine* engine,
                                             std::string c_src,
                                             std::string t_src,
                                             std::string n_src,
                                             std::string b_src)
    : engine_(engine)
    , src_{std::move(c_src), std::move(t_src), std::move(n_src), std::move(b_src)}
{}

float lets_splosh_modulator::read_cv_src(const std::string& src,
                                          float fallback) const noexcept {
    if (engine_ && !src.empty()) {
        const auto* o = engine_->last_output(src);
        if (o) return o->cv;
    }
    return fallback;
}

modulator_output lets_splosh_modulator::tick(double /*beat*/, float /*tick_rate_hz*/) {
    // Resolve 4 inputs.
    float ins[4];
    for (int i = 0; i < 4; ++i)
        ins[i] = read_cv_src(src_[i], inputs_[i]);

    modulator_output out;

    // Compute all 16 partition outputs.
    // outputs[m] = max(0, pos_sum(m) − neg_sum(m))
    // where pos_sum(m) = sum of ins[i] for bits set in m.
    for (int m = 0; m < 16; ++m) {
        float pos = 0.0f, neg = 0.0f;
        for (int i = 0; i < 4; ++i) {
            if ((m >> i) & 1) pos += ins[i];
            else              neg += ins[i];
        }
        out.outputs[m] = std::max(0.0f, pos - neg);
    }

    // cv = outputs[0b0111] = max(0, C+T+N − B)
    // aux = outputs[0b1111] = max(0, C+T+N+B)
    out.cv   = out.outputs[0x7];
    out.aux  = out.outputs[0xF];
    out.gate = out.aux > 0.0f;

    uint32_t bitmap = 0;
    for (int i = 0; i < 4; ++i)
        if (ins[i] > 0.5f) bitmap |= (1u << i);
    out.state = bitmap;

    return out;
}

void lets_splosh_modulator::update(std::string_view key, float value) {
    if      (key == "c") inputs_[0] = value;
    else if (key == "t") inputs_[1] = value;
    else if (key == "n") inputs_[2] = value;
    else if (key == "b") inputs_[3] = value;
}

} // namespace nomos::rt
