// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <nomos/rt/abstract_modulator.hpp>

#include <string>
#include <string_view>

namespace nomos::rt {

class modulator_engine;

// Dual-clock XOR division polyrhythm generator.
//
// Two independent 4-bit ripple binary counters are advanced by their
// respective clock inputs. The four XOR outputs are:
//   out[i] = bit i of counter1 XOR bit i of counter2   (i = 0..3)
// which corresponds to ÷2, ÷4, ÷8, ÷16 XOR interference patterns.
//
// With a single clock (clock2 absent / counter2 stays at zero): XOR with
// zero is identity — outputs are pure ÷2, ÷4, ÷8, ÷16 gates.
// With two clocks at different rates: XOR encodes the phase difference
// between corresponding division stages (ring modulation in the gate domain).
//
// MAIN = gain-weighted sum of active XOR outputs, normalised to [0, 1]:
//   raw = sum(gains[i] × out[i]);  main = raw / sum(gains)
// SLEW = one-pole IIR on MAIN; slew=0 instant, slew=1 ≈ 4-second glide.
//
// Clock sources: pass a non-empty source_id at construction to detect rising
// edges on engine->last_output(source_id).gate each tick.  Manual one-shot
// edges are injected via update("clock1_tick", 1.0f) / update("clock2_tick", 1.0f).
//
// Outputs (modulator_output):
//   .cv    — MAIN normalised [0, 1]
//   .aux   — SLEW (IIR-smoothed MAIN) [0, 1]
//   .gate  — out[0]  (÷2 XOR gate)
//   .gate2 — out[1]  (÷4 XOR gate)
//   .state — 4-bit XOR bitmap (bit i = out[i])
//
// Parameters (via update()):
//   "gain0".."gain3"  — per-division weight [0, 5]   (default 1.0)
//   "slew"            — IIR coefficient    [0, 1]    (default 0.0)
//   "clock1_tick"     — >0.5 arms one clock-1 edge (one-shot)
//   "clock2_tick"     — >0.5 arms one clock-2 edge (one-shot)
class divine_cmos_modulator final : public abstract_modulator {
public:
    explicit divine_cmos_modulator(const modulator_engine* engine     = nullptr,
                                   std::string             clock1_src = {},
                                   std::string             clock2_src = {});

    modulator_output tick(double beat, float tick_rate_hz) override;
    void             update(std::string_view key, float value) override;

private:
    bool read_gate(const std::string& src_id, bool& prev_gate) const noexcept;

    const modulator_engine* engine_;
    std::string             clock1_src_;
    std::string             clock2_src_;

    float gains_[4]{1.0f, 1.0f, 1.0f, 1.0f};
    float slew_{0.0f};

    uint8_t counter1_{0};
    uint8_t counter2_{0};

    bool clock1_prev_{false};
    bool clock2_prev_{false};
    bool clock1_pending_{false};
    bool clock2_pending_{false};

    float slew_state_{0.0f};
};

} // namespace nomos::rt
