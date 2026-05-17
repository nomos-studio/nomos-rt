// SPDX-License-Identifier: LGPL-2.1-or-later
#include "stochastic_modulator.hpp"

#include <algorithm>
#include <cmath>

namespace nomos::rt {

stochastic_modulator::stochastic_modulator() {
    // Seed buffer with distinct values so first playback is immediately varied.
    for (int i = 0; i < kMaxLength; ++i) {
        cv_buf_[i]   = next_rand() * 2.0f - 1.0f;
        gate_buf_[i] = next_rand() > 0.5f;
    }
}

modulator_output stochastic_modulator::tick(double /*beat*/, float tick_rate_hz) {
    gate_out_ = false;

    if (tick_rate_hz <= 0.0f)
        return {.cv = cv_out_ * depth_};

    clock_phase_ += rate_ / tick_rate_hz;

    if (clock_phase_ < next_threshold_)
        return {.cv = cv_out_ * depth_};

    clock_phase_ -= next_threshold_;

    const float u = std::max(next_rand(), 1e-6f);
    next_threshold_ = std::lerp(1.0f, -std::log(u), jitter_);

    const float prob_replay = std::min(deja_vu_ * 2.0f, 1.0f);
    const float prob_freeze = std::max((deja_vu_ - 0.5f) * 2.0f, 0.0f);

    const bool replay = (deja_vu_ > 0.0f) && (next_rand() < prob_replay);
    const bool freeze = (deja_vu_ > 0.5f) && (next_rand() < prob_freeze);

    if (!replay) {
        const bool  new_gate = next_rand() < bias_;
        const float new_cv   = generate_cv();
        gate_buf_[write_pos_] = new_gate;
        cv_buf_[write_pos_]   = new_cv;
        write_pos_ = (write_pos_ + 1) % length_;
    }

    gate_out_ = gate_buf_[read_pos_];
    cv_out_   = cv_buf_[read_pos_];

    if (!freeze)
        read_pos_ = (read_pos_ + 1) % length_;

    return {.cv = cv_out_ * depth_, .gate = gate_out_};
}

void stochastic_modulator::update(std::string_view key, float value) {
    if      (key == "rate")    rate_    = std::clamp(value, 0.001f, 100.0f);
    else if (key == "bias")    bias_    = std::clamp(value, 0.0f,   1.0f);
    else if (key == "jitter")  jitter_  = std::clamp(value, 0.0f,   1.0f);
    else if (key == "spread")  spread_  = std::clamp(value, 0.0f,   1.0f);
    else if (key == "deja_vu") deja_vu_ = std::clamp(value, 0.0f,   1.0f);
    else if (key == "depth")   depth_   = std::clamp(value, 0.0f,   1.0f);
    else if (key == "length")  length_  = std::clamp(static_cast<int>(std::round(value)), 1, kMaxLength);
    else if (key == "steps")   steps_   = std::clamp(static_cast<int>(std::round(value)), 0, 16);
}

float stochastic_modulator::next_rand() noexcept {
    rand_state_ = rand_state_ * 1664525u + 1013904223u;
    return static_cast<float>(rand_state_ >> 8) * (1.0f / 16777216.0f);
}

float stochastic_modulator::generate_cv() noexcept {
    const float raw = next_rand();
    // Centre on bias, half-width = spread.
    float cv = bias_ + (raw - 0.5f) * spread_ * 2.0f;
    cv = std::clamp(cv, 0.0f, 1.0f);
    if (steps_ > 0)
        cv = std::round(cv * static_cast<float>(steps_)) / static_cast<float>(steps_);
    // Normalise to [-1, 1].
    return cv * 2.0f - 1.0f;
}

} // namespace nomos::rt
