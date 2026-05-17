// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "abstract_modulator.hpp"

#include <cstdint>
#include <string_view>

namespace nomos::rt {

// Stochastic gate and CV source, inspired by the Marbles T/X sections.
//
// Combines two channels in one modulator:
//
//   T channel (gates): a clock at `rate` Hz fires gates with probability
//   `bias`.  `jitter` smoothly morphs the inter-event distribution from
//   perfectly periodic (jitter=0) to Poisson/exponential (jitter=1).
//
//   X channel (CV): on each gate event a random voltage is drawn from a
//   distribution centred on `bias` with half-width `spread`, then held until
//   the next event.  `steps` quantises the output to equal steps.
//
// Déjà vu loop (shared for T and X):
//   Both channels maintain a circular buffer of `length` recent values.
//   `deja_vu` controls how often the buffer replays vs. generates new values:
//     0.0 — always generate new; loop never replays
//     0.5 — always replay from loop (locked loop)
//     1.0 — loop frozen; read position does not advance
//   Between 0 and 0.5: probability of replay = deja_vu × 2.
//   Between 0.5 and 1: replay always, but freeze probability = (deja_vu-0.5)×2.
//
// CV output is normalised to [-1, 1] before depth scaling.
// gate_out() is true for exactly the tick on which a gate event fired.
//
// Parameters (via update()):
//   "rate"     — clock rate Hz           [0.001, 100]  (default 2.0)
//   "bias"     — gate probability / CV centre  [0, 1]  (default 0.5)
//   "jitter"   — timing variation        [0, 1]        (default 0.0)
//   "spread"   — CV half-range           [0, 1]        (default 0.5)
//   "deja_vu"  — loop parameter          [0, 1]        (default 0.0)
//   "length"   — loop buffer length      [1, 16]       (default 8)
//   "steps"    — CV quantisation steps   [0, 16]       (default 0 = continuous)
//   "depth"    — output scale            [0, 1]        (default 1.0)
class stochastic_modulator final : public abstract_modulator {
public:
    explicit stochastic_modulator();

    float tick(double beat, float tick_rate_hz) override;
    void  update(std::string_view key, float value) override;

    bool gate_out() const noexcept { return gate_out_; }

private:
    static constexpr int kMaxLength = 16;

    float next_rand() noexcept;
    float generate_cv() noexcept;

    float rate_{2.0f};
    float bias_{0.5f};
    float jitter_{0.0f};
    float spread_{0.5f};
    float deja_vu_{0.0f};
    float depth_{1.0f};
    int   length_{8};
    int   steps_{0};

    // Clock state
    float    clock_phase_{0.0f};
    float    next_threshold_{1.0f};  // current period length (jitter-modified)

    // CV output
    float    cv_out_{0.0f};
    bool     gate_out_{false};

    // Déjà vu loop buffers
    float    cv_buf_[kMaxLength]{};
    bool     gate_buf_[kMaxLength]{};
    int      write_pos_{0};
    int      read_pos_{0};

    uint32_t rand_state_{0xACE1u};
};

} // namespace nomos::rt
