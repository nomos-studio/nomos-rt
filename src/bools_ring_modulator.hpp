// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <nomos/rt/abstract_modulator.hpp>

#include <string>
#include <string_view>

namespace nomos::rt {

class modulator_engine;

// Boolean ring-topology gate processor with 4-bit DAC STEP output.
//
// The selected logic function is applied to adjacent input pairs in a ring:
//   out[0] = logic(in[0], in[1])
//   out[1] = logic(in[1], in[2])
//   out[2] = logic(in[2], in[3])
//   out[3] = logic(in[3], in[0])   ← ring wrap-around
//
// Each input participates in exactly two output computations.
// Supported modes: xor, or, and, nor, nand, xnor.
//
// STEP output: non-uniform 4-bit DAC:
//   step = out[0]×0.55 + out[1]×1.09 + out[2]×2.19 + out[3]×4.37
// Normalised to [0, 1] by dividing by the maximum (8.20).
// The doubling-weight ladder means high-index outputs dominate.
//
// SLEW: one-pole IIR on STEP; slew=0 instant, slew=1 ≈ 4-second glide.
//
// SAMPLE mode: when enabled and a sample source is set, logic computation is
// frozen between rising edges of the sample clock.
//
// Inputs (in[0..3]) are read from cross-modulator gate sources or set directly
// via update("in0".."in3", value). >0.5 = logical true.
//
// Outputs (modulator_output):
//   .cv    — STEP normalised [0, 1]
//   .aux   — SLEW (IIR-smoothed STEP) [0, 1]
//   .gate  — out[0]
//   .gate2 — out[1]
//   .state — 4-bit output bitmap (bit i = out[i])
//
// Parameters (via update()):
//   "in0".."in3"  — gate inputs (>0.5 = high)   (default 0.0)
//   "slew"        — IIR coefficient [0, 1]       (default 0.0)
//   "sampled"     — >0.5 enables sample-clock latch
//   "sample_tick" — >0.5 arms one sample edge (one-shot)
class bools_ring_modulator final : public abstract_modulator {
public:
    enum class mode { xor_mode, or_mode, and_mode, nor_mode, nand_mode, xnor_mode };

    // src_ids[0..3]: cross-modulator source IDs for in[0..3] (empty = use update()).
    // sample_src: cross-modulator source for sample clock gate (empty = use sampled/sample_tick).
    explicit bools_ring_modulator(mode        m          = mode::xor_mode,
                                  const modulator_engine* engine = nullptr,
                                  std::string src0       = {},
                                  std::string src1       = {},
                                  std::string src2       = {},
                                  std::string src3       = {},
                                  std::string sample_src = {});

    modulator_output tick(double beat, float tick_rate_hz) override;
    void             update(std::string_view key, float value) override;

private:
    bool apply_logic(bool a, bool b) const noexcept;
    bool read_gate(const std::string& src_id, float fallback) const noexcept;

    mode                    mode_;
    const modulator_engine* engine_;
    std::string             src_[4];
    std::string             sample_src_;

    float inputs_[4]{};
    float slew_{0.0f};
    bool  sampled_{false};
    bool  sample_pending_{false};
    bool  sample_prev_{false};

    float slew_state_{0.0f};
    uint8_t latched_out_{0};

    static constexpr float kStepWeights[4] = {0.55f, 1.09f, 2.19f, 4.37f};
    static constexpr float kStepMax        = 0.55f + 1.09f + 2.19f + 4.37f;  // 8.20
};

} // namespace nomos::rt
