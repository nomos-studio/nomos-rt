// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "abstract_modulator.hpp"

#include <string_view>

namespace nomos::rt {

// Slew-limiter / function generator with cycle mode.
//
// Two operating modes selected implicitly by state:
//
//   Lag mode  — no active stage, no cycle.  Output follows input_ with
//               independent rise and fall time constants (one-pole IIR).
//               Useful for portamento, lag, and CV smoothing.
//
//   Generator — an active rise/fall stage is running (started by trig or
//               cycle enable).  Output moves autonomously between −1 and +1
//               at the configured rise/fall rates, ignoring input_.
//
// Stage machine (generator):
//   rising  → output approaches +1 at rate 1/rise_
//   falling → output approaches −1 at rate 1/fall_
//   idle    → waiting; returns to lag mode
//
// Cycle mode: at end of fall, immediately re-enter rising instead of idle.
// Produces a continuous asymmetric oscillator (Maths/Serge-style).
//
// Trig: snaps output to −1 and starts rising, regardless of current state.
// In cycle mode, trig resets the cycle phase.
//
// eor()/eoc() fire for exactly one tick at the top/bottom transition.
// They do not fire in lag mode.
//
// Parameters (via update()):
//   "rise"  — rise time [s]   [0.001, 10]   (default 0.1)
//   "fall"  — fall time [s]   [0.001, 10]   (default 0.1)
//   "input" — lag target      [-1, 1]        (default 0)
//   "depth" — output scale    [0, 1]         (default 1)
//   "cycle" — > 0.5 enables free-running     (default off)
//   "trig"  — > 0.5 arms a one-shot cycle
class slew_modulator final : public abstract_modulator {
public:
    explicit slew_modulator() = default;

    modulator_output tick(double beat, float tick_rate_hz) override;
    void             update(std::string_view key, float value) override;

private:
    enum class stage { idle, rising, falling };

    float coeff(float time_s, float tick_rate_hz) const noexcept;

    float rise_{0.1f};
    float fall_{0.1f};
    float input_{0.0f};
    float depth_{1.0f};
    bool  cycle_{false};

    stage stage_{stage::idle};
    float current_{0.0f};
    bool  trig_pending_{false};
    bool  eor_{false};
    bool  eoc_{false};
};

} // namespace nomos::rt
