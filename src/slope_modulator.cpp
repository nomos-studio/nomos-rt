// SPDX-License-Identifier: LGPL-2.1-or-later
#include "slope_modulator.hpp"

#include "tides/generator.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace nomos::rt {

// Pitch-to-frequency calibration for tides::Generator at RANGE_MEDIUM:
//   set_pitch(7680) at firmware SR 31250 Hz → ≈ 2.15 Hz
//
// To hit rate_hz at a virtual SR of tick_rate_hz:
//   pitch = 7680 + kOctave * log2(rate_hz * 31250 / (CALIB_HZ * tick_rate_hz))
//
// kOctave = 12 * 128 = 1536
static constexpr float kFirmwareSR  = 31250.0f;
static constexpr float kCalibHzAt7680 = 2.15f; // Hz at pitch=7680, RANGE_MEDIUM, 31250 SR
static constexpr float kOctave       = 1536.0f;  // semitone units per octave
static constexpr float kCalibPitch   = 7680.0f;

slope_modulator::slope_modulator() {
    gen_.Init();
    gen_.set_range(tides::GENERATOR_RANGE_MEDIUM);
    gen_.set_mode(tides::GENERATOR_MODE_LOOPING);
    gen_.set_shape(0);
    gen_.set_slope(0);
    gen_.set_smoothness(0);
    // Pre-render so the first playback block is ready.
    gen_.Process();
}

float slope_modulator::tick(double /*beat*/, float tick_rate_hz) {
    if (tick_rate_hz != last_tick_rate_) {
        apply_rate(tick_rate_hz);
        last_tick_rate_ = tick_rate_hz;
    }

    // Ensure next block is rendered before we consume from it.
    gen_.Process();

    // Consume one virtual sample; control=0 means free-running looping.
    const tides::GeneratorSample& s = gen_.Process(static_cast<uint8_t>(0));

    if (bipolar_)
        return static_cast<float>(s.bipolar) * (1.0f / 32768.0f) * depth_;
    else
        return static_cast<float>(s.unipolar) * (1.0f / 65535.0f) * depth_;
}

void slope_modulator::update(std::string_view key, float value) {
    if (key == "rate") {
        rate_hz_ = std::clamp(value, 0.001f, 100.0f);
        last_tick_rate_ = 0.0f; // force recalibration on next tick
    } else if (key == "shape") {
        shape_ = std::clamp(value, -1.0f, 1.0f);
        gen_.set_shape(static_cast<int16_t>(shape_ * 32767.0f));
    } else if (key == "slope") {
        slope_ = std::clamp(value, -1.0f, 1.0f);
        gen_.set_slope(static_cast<int16_t>(slope_ * 32767.0f));
    } else if (key == "smoothness") {
        smoothness_ = std::clamp(value, -1.0f, 1.0f);
        gen_.set_smoothness(static_cast<int16_t>(smoothness_ * 32767.0f));
    } else if (key == "depth") {
        depth_ = std::clamp(value, 0.0f, 1.0f);
    } else if (key == "bipolar") {
        bipolar_ = (value >= 0.5f);
    }
}

void slope_modulator::apply_rate(float tick_rate_hz) {
    if (tick_rate_hz <= 0.0f)
        return;
    // See calibration note at top of file.
    const float pitch_f =
        kCalibPitch + kOctave * std::log2(rate_hz_ * kFirmwareSR / (kCalibHzAt7680 * tick_rate_hz));
    const auto pitch = static_cast<int16_t>(
        std::clamp(pitch_f, static_cast<float>(INT16_MIN), static_cast<float>(INT16_MAX)));
    gen_.set_pitch(pitch);
}

} // namespace nomos::rt
