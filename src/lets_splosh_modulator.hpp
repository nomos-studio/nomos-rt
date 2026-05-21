// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <nomos/rt/abstract_modulator.hpp>

#include <string>
#include <string_view>

namespace nomos::rt {

class modulator_engine;

// Let's Splosh combinatorial difference-rectifier analysis of 4 signals.
//
// Hardware reference: NLC Let's Splosh.
//
// All 16 ways of assigning 4 inputs (C, T, N, B) to a positive or negative
// group are computed every tick.  For each assignment m (a 4-bit mask):
//
//   outputs[m] = max(0, pos_sum(m) − neg_sum(m))
//
// where pos_sum(m) = sum of inputs[i] where bit i of m is set, and
//       neg_sum(m) = sum of inputs[i] where bit i of m is NOT set.
//
// masks 0000 (all-negative) → always 0.
// mask  1111 (all-positive) → half-wave-rectified sum of all 4 inputs.
// mask m and mask ~m are "partner" partitions; only one can be non-zero.
//
// Uses kMaxOutputs = 16 (all 16 partition outputs stored in outputs[]).
//
// Inputs read from cross-modulator CV outputs if src IDs are provided,
// or set directly via update("c"/"t"/"n"/"b", value).
//
// Parameters (via update()):
//   "c" / "t" / "n" / "b" — input values [0, 1]
//
// Outputs (modulator_output):
//   .cv         — outputs[0b0111] = max(0, C+T+N − B)
//   .aux        — outputs[0b1111] = max(0, C+T+N+B)
//   .gate       — outputs[0b1111] > 0  (any input active)
//   .state      — mask of inputs currently above 0.5
//   .outputs[m] — partition output for 4-bit mask m  (m = 0..15)
class lets_splosh_modulator final : public abstract_modulator {
public:
    explicit lets_splosh_modulator(const modulator_engine* engine  = nullptr,
                                   std::string             c_src   = {},
                                   std::string             t_src   = {},
                                   std::string             n_src   = {},
                                   std::string             b_src   = {});

    modulator_output tick(double beat, float tick_rate_hz) override;
    void             update(std::string_view key, float value) override;

private:
    float read_cv_src(const std::string& src, float fallback) const noexcept;

    const modulator_engine* engine_;
    std::string             src_[4];  // c, t, n, b sources

    float inputs_[4]{};
};

} // namespace nomos::rt
