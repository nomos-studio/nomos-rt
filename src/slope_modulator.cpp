// SPDX-License-Identifier: LGPL-2.1-or-later
#include "slope_modulator.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace nomos::rt {

namespace {
constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;
} // namespace

float slope_modulator::tick(double /*beat*/, float tick_rate_hz) {
    if (tick_rate_hz <= 0.0f)
        return last_output_;

    phase_ += rate_ / tick_rate_hz;
    if (phase_ >= 1.0f) phase_ -= 1.0f;

    // Slope skew: map raw phase to a skewed phase where the peak occurs at
    // `attack` instead of 0.5.  attack ∈ [0.01, 0.99].
    const float attack = std::clamp(0.5f + slope_ * 0.49f, 0.01f, 0.99f);
    float p;
    if (phase_ < attack)
        p = phase_ / attack * 0.5f;
    else
        p = 0.5f + (phase_ - attack) / (1.0f - attack) * 0.5f;
    // p ∈ [0, 1]; peak at p = 0.5

    // Shape morph: triangle → sine → peaked exponential.
    const float tri   = (p < 0.5f) ? p * 2.0f : (1.0f - p) * 2.0f;
    const float sin_v = 0.5f - 0.5f * std::cos(kTwoPi * p);

    float output;
    if (shape_ <= 0.0f)
        output = tri + (shape_ + 1.0f) * (sin_v - tri);  // lerp tri→sine
    else
        output = std::pow(std::max(sin_v, 1e-6f), 1.0f + shape_ * 2.0f); // sine→peaked

    // Smoothness: < 0 → one-pole LP; > 0 → wavefold.
    if (smoothness_ < 0.0f) {
        const float c = 1.0f + smoothness_;  // 0 at -1, 1 at 0
        smooth_state_ += c * c * (output - smooth_state_);
        output = smooth_state_;
    } else if (smoothness_ > 0.0f) {
        float x = output * (1.0f + smoothness_ * 3.0f);
        x = std::fmod(x, 2.0f);
        if (x > 1.0f) x = 2.0f - x;
        output = x;
    }

    output = bipolar_ ? output * 2.0f - 1.0f : output;
    return last_output_ = output * depth_;
}

void slope_modulator::update(std::string_view key, float value) {
    if      (key == "rate")       rate_       = std::clamp(value, 0.001f, 100.0f);
    else if (key == "shape")      shape_      = std::clamp(value, -1.0f, 1.0f);
    else if (key == "slope")      slope_      = std::clamp(value, -1.0f, 1.0f);
    else if (key == "smoothness") smoothness_ = std::clamp(value, -1.0f, 1.0f);
    else if (key == "depth")      depth_      = std::clamp(value, 0.0f,  1.0f);
    else if (key == "bipolar")    bipolar_    = (value >= 0.5f);
}

} // namespace nomos::rt
