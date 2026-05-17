// SPDX-License-Identifier: LGPL-2.1-or-later
#include "fractal_modulator.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace nomos::rt {

namespace {
constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;

// Distinct seeds and golden-ratio-spaced initial phases so all octaves start
// in different positions — avoids the all-in-phase cancellation at tick 0.
constexpr uint32_t kSeeds[8] = {
    0xACE1u, 0xDEADu, 0xBEEFu, 0xCAFEu,
    0x1234u, 0x5678u, 0x9ABCu, 0xDEF0u,
};
constexpr float kPhi = 0.618034f;  // golden ratio fractional part
} // namespace

fractal_modulator::fractal_modulator() {
    for (int i = 0; i < kMaxOctaves; ++i) {
        rand_[i]  = kSeeds[i];
        phase_[i] = std::fmod(static_cast<float>(i) * kPhi, 1.0f);
        held_[i]  = next_rand(i) * 2.0f - 1.0f;
    }
}

modulator_output fractal_modulator::tick(double /*beat*/, float tick_rate_hz) {
    gate_out_ = false;

    if (tick_rate_hz <= 0.0f)
        return {.cv = prev_output_ * depth_};

    float sum  = 0.0f;
    float norm = 0.0f;
    float freq = base_rate_;
    float amp  = 1.0f;

    for (int i = 0; i < octaves_; ++i) {
        const float inc = freq / tick_rate_hz;

        if (shape_ == 2 && phase_[i] + inc >= 1.0f)
            held_[i] = next_rand(i) * 2.0f - 1.0f;

        phase_[i] = std::fmod(phase_[i] + inc, 1.0f);

        sum  += amp * sample(i);
        norm += amp;

        freq *= lacunarity_;
        amp  *= persistence_;
    }

    const float output = (norm > 0.0f) ? (sum / norm) : 0.0f;

    if (output >= threshold_ && prev_output_ < threshold_)
        gate_out_ = true;

    prev_output_ = output;
    return {.cv = std::clamp(output, -1.0f, 1.0f) * depth_, .gate = gate_out_};
}

void fractal_modulator::update(std::string_view key, float value) {
    if      (key == "base_rate")   base_rate_   = std::clamp(value, 0.001f, 100.0f);
    else if (key == "octaves")     octaves_     = std::clamp(static_cast<int>(std::round(value)), 1, kMaxOctaves);
    else if (key == "lacunarity")  lacunarity_  = std::clamp(value, 1.1f, 8.0f);
    else if (key == "persistence") persistence_ = std::clamp(value, 0.01f, 2.0f);
    else if (key == "shape")       shape_       = std::clamp(static_cast<int>(std::round(value)), 0, 2);
    else if (key == "threshold")   threshold_   = std::clamp(value, -1.0f, 1.0f);
    else if (key == "depth")       depth_       = std::clamp(value, 0.0f,  1.0f);
}

float fractal_modulator::sample(int i) const noexcept {
    switch (shape_) {
        case 1:  return 1.0f - 4.0f * std::abs(phase_[i] - 0.5f);  // triangle
        case 2:  return held_[i];                                     // S&H
        default: return std::sin(kTwoPi * phase_[i]);                 // sine
    }
}

float fractal_modulator::next_rand(int i) noexcept {
    rand_[i] = rand_[i] * 1664525u + 1013904223u;
    return static_cast<float>(rand_[i] >> 8) * (1.0f / 16777216.0f);
}

} // namespace nomos::rt
