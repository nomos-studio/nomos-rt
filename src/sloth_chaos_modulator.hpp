// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <nomos/rt/abstract_modulator.hpp>

#include <string>
#include <string_view>

namespace nomos::rt {

class modulator_engine;

// Sloth Chaos strange-attractor oscillator.
//
// Physics-based emulation of the NLC Sloth Chaos circuit (Andrew Fitch).
// Circuit analysis by Don Cross; VCV Rack port by Michael Hetrick (OSS).
//
// Three coupled ODEs (x, w, y node voltages) drive a fourth node z that is an
// instantaneous comparator switching between −10.64 V and +11.38 V.  The
// comparator switching creates a two-lobe attractor: the system orbits one basin
// while z is positive and the other while z is negative.
//
// Speed variants select a timeDilation factor that scales effective dt without
// changing the attractor's qualitative character:
//   torpor  : timeDilation = 1.0     → ~15–30 s per orbit
//   apathy  : timeDilation = 0.274   → ~60–90 s per orbit
//   inertia : timeDilation = 0.0097  → ~30–40 min per orbit
//   triple  : all three run simultaneously; a combined output is also produced
//
// Integration: 4 Euler sub-steps per tick (tick_rate_hz typically 375 Hz).
//
// Parameters (via update()):
//   "knob"   — lobe-balance bias [0, 1] (default 0.5 = neutral)
//   "cv_in"  — voltage nudge to trajectory (from cross-modulator or direct)
//
// Outputs (modulator_output):
//   .cv    — x normalised to [0, 1]
//   .aux   — y normalised to [0, 1]
//   .gate  — current attractor lobe (z > 0 = true)
//   .gate2 — lobe transition pulse (fires for one tick on sign change of z)
//   .state — 0 = negative lobe, 1 = positive lobe
//   .outputs[0] = x_norm  .outputs[1] = y_norm
//   .outputs[2] = w_norm  .outputs[3] = z_norm (0 = −10.64 V, 1 = +11.38 V)
//
// Triple variant — also fills:
//   .outputs[0..1] = torpor x/y
//   .outputs[2..3] = apathy x/y
//   .outputs[4..5] = inertia x/y
//   .outputs[6]    = combined = 0.7 × (torpor_z − (apathy_z + inertia_z)), norm
class sloth_chaos_modulator final : public abstract_modulator {
public:
    enum class variant { torpor, apathy, inertia, triple };

    explicit sloth_chaos_modulator(variant v = variant::torpor,
                                   const modulator_engine* engine   = nullptr,
                                   std::string             cv_src   = {});

    modulator_output tick(double beat, float tick_rate_hz) override;
    void             update(std::string_view key, float value) override;

private:
    struct State {
        float x{0.01f}, w{0.0f}, y{0.0f};
        float z{11.38f};
        bool  z_prev_positive{true};

        void step(float h, float K, float z_bias, float cv_in) noexcept;
        float x_norm() const noexcept;
        float y_norm() const noexcept;
        float w_norm() const noexcept;
    };

    variant                 variant_;
    const modulator_engine* engine_;
    std::string             cv_src_;

    State states_[3];  // [0]=torpor, [1]=apathy, [2]=inertia

    float knob_{0.5f};
    float cv_in_{0.0f};

    static constexpr float kTimeDilation[3] = {1.0f, 0.274f, 0.0097f};
    static constexpr float kAlpha = 0.35f;
    static constexpr float kBeta  = 0.25f;
    static constexpr float kGamma = 0.15f;
    static constexpr float kVRange = 12.0f;     // ±12 V → normalise to [0,1]
    static constexpr float kZHigh  = 11.38f;
    static constexpr float kZLow   = -10.64f;
};

} // namespace nomos::rt
