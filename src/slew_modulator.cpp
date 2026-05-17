// SPDX-License-Identifier: LGPL-2.1-or-later
#include "slew_modulator.hpp"

#include <algorithm>
#include <cmath>

namespace nomos::rt {

// One-pole IIR coefficient: output reaches 90% of target in time_s seconds.
// exp(-2.2) ≈ 0.1, so the 10%-to-90% slew time equals time_s.
float slew_modulator::coeff(float time_s, float tick_rate_hz) const noexcept {
    return 1.0f - std::exp(-2.2f / (time_s * tick_rate_hz));
}

modulator_output slew_modulator::tick(double /*beat*/, float tick_rate_hz) {
    eor_ = eoc_ = false;

    if (tick_rate_hz <= 0.0f)
        return {.cv = current_ * depth_};

    if (trig_pending_) {
        trig_pending_ = false;
        current_ = -1.0f;
        stage_    = stage::rising;
    }

    if (cycle_ && stage_ == stage::idle)
        stage_ = stage::rising;

    if (stage_ == stage::idle) {
        const float target = input_;
        const float time_s = (target >= current_) ? rise_ : fall_;
        current_ += (target - current_) * coeff(time_s, tick_rate_hz);
    } else {
        const float target = (stage_ == stage::rising) ? 1.0f : -1.0f;
        const float time_s = (stage_ == stage::rising) ? rise_ : fall_;
        current_ += (target - current_) * coeff(time_s, tick_rate_hz);

        constexpr float kThresh = 1e-3f;
        if (stage_ == stage::rising && current_ >= 1.0f - kThresh) {
            current_ = 1.0f;
            eor_     = true;
            stage_   = stage::falling;
        } else if (stage_ == stage::falling && current_ <= -1.0f + kThresh) {
            current_ = -1.0f;
            eoc_     = true;
            stage_   = stage::idle;
        }
    }

    return {.cv = current_ * depth_, .gate = eor_, .gate2 = eoc_};
}

void slew_modulator::update(std::string_view key, float value) {
    if      (key == "rise")              rise_  = std::clamp(value, 0.001f, 10.0f);
    else if (key == "fall")              fall_  = std::clamp(value, 0.001f, 10.0f);
    else if (key == "input")             input_ = std::clamp(value, -1.0f,  1.0f);
    else if (key == "depth")             depth_ = std::clamp(value, 0.0f,   1.0f);
    else if (key == "cycle")             cycle_ = value > 0.5f;
    else if (key == "trig" && value > 0.5f) trig_pending_ = true;
}

} // namespace nomos::rt
