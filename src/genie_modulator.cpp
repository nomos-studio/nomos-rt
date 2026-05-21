// SPDX-License-Identifier: LGPL-2.1-or-later
#include "genie_modulator.hpp"

#include <nomos/rt/modulator_engine.hpp>

#include <algorithm>
#include <cstring>

namespace nomos::rt {

genie_modulator::genie_modulator(int n, const modulator_engine* engine,
                                  std::string in_src[kMaxN])
    : engine_(engine)
    , n_(std::clamp(n, 2, kMaxN))
{
    for (int i = 0; i < kMaxN; ++i) {
        sense_[i]    = 2.5f;
        response_[i] = 3.0f;
        gain_[i]     = 0.6f;
        if (in_src) in_src_[i] = std::move(in_src[i]);
    }
}

float genie_modulator::neuron(float input, float sense, float response) noexcept {
    const float x    = input + sense;
    const float rect = std::clamp(x, 0.0f, 10.0f);
    const float comp = (x >= 0.0f) ? response : -response;
    return rect - comp;
}

float genie_modulator::neuron_norm(float raw, int i) const noexcept {
    // Range: [-response, 10-response] above threshold, [+response] below.
    // Full range: [-response, max(response, 10-response)].
    const float lo = -response_[i];
    const float hi = std::max(response_[i], 10.0f - response_[i]);
    if (hi == lo) return 0.5f;
    return std::clamp((raw - lo) / (hi - lo), 0.0f, 1.0f);
}

float genie_modulator::read_cv_src(const std::string& src,
                                    float fallback) const noexcept {
    if (engine_ && !src.empty()) {
        const auto* o = engine_->last_output(src);
        if (o) return o->cv;
    }
    return fallback;
}

modulator_output genie_modulator::tick(double /*beat*/, float /*tick_rate_hz*/) {
    // Resolve external inputs (or use previous state_ as ring signal).
    float inputs[kMaxN];
    for (int i = 0; i < n_; ++i)
        inputs[i] = in_src_[i].empty()
                    ? gain_[i] * state_[(i + n_ - 1) % n_]   // ring: prev neuron
                    : read_cv_src(in_src_[i], ext_in_[i]);

    // Compute all neurons simultaneously from previous state (no intra-tick
    // feedback — matches one-clock-latency ring behaviour of the hardware).
    float new_state[kMaxN]{};
    for (int i = 0; i < n_; ++i)
        new_state[i] = neuron(inputs[i], sense_[i], response_[i]);

    for (int i = 0; i < n_; ++i)
        state_[i] = new_state[i];

    // Diff-Rect: (N1 + N3) − N2, using neurons 0, 1, 2.
    // Guard for N=2: use (N1 + N1) − N2 (degenerate).
    const float n3    = (n_ >= 3) ? state_[2] : state_[0];
    const float diff  = state_[0] + n3 - state_[1];
    const float d_pos = std::max(0.0f, diff);

    // Normalise diff-rect.  Theoretical max positive = 2×(10-response) + response
    // ≈ 17 for response=3; clamp to [0,1] by dividing by 17.
    const float d_pos_norm = std::clamp(d_pos / 17.0f, 0.0f, 1.0f);

    modulator_output out;
    out.cv    = neuron_norm(state_[0], 0);
    out.aux   = d_pos_norm;
    out.gate  = diff > 0.0f;
    out.gate2 = diff < 0.0f;

    uint32_t bitmap = 0;
    for (int i = 0; i < n_; ++i) {
        out.outputs[i] = neuron_norm(state_[i], i);
        if (state_[i] > 0.0f) bitmap |= (1u << i);
    }
    out.state = bitmap;

    return out;
}

void genie_modulator::update(std::string_view key, float value) {
    // "sense0".."sense7", "response0".."response7", "gain0".."gain7", "in0".."in7"
    if (key.size() >= 5 && key.substr(0, 5) == "sense") {
        const int i = key.back() - '0';
        if (i >= 0 && i < kMaxN) sense_[i] = std::clamp(value, 0.0f, 5.0f);
    } else if (key.size() >= 8 && key.substr(0, 8) == "response") {
        const int i = key.back() - '0';
        if (i >= 0 && i < kMaxN) response_[i] = std::clamp(value, 1.0f, 10.0f);
    } else if (key.size() >= 4 && key.substr(0, 4) == "gain") {
        const int i = key.back() - '0';
        if (i >= 0 && i < kMaxN) gain_[i] = std::clamp(value, 0.0f, 1.0f);
    } else if (key.size() >= 2 && key.substr(0, 2) == "in") {
        const int i = key.back() - '0';
        if (i >= 0 && i < kMaxN) ext_in_[i] = value;
    }
}

} // namespace nomos::rt
