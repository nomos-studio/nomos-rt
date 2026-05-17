// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "abstract_modulator.hpp"

#include <cstdint>
#include <string_view>

namespace nomos::rt {

// Clocked shift register with four feedback modes.
//
// An N-bit shift register (2–32) that advances one position on each internal
// clock edge.  A new bit enters the LSB according to the selected mode; the
// register shifts left.  A configurable R2R DAC reads dac_bits_ most-significant
// bits and produces a stepped CV output normalised to [-1, 1].
//
// Mode is fixed at construction (patch-declaration time).  Clock rate, data,
// param, and depth are live via update().
//
// Modes:
//   lfsr    — XOR of fixed maximal-length polynomial taps; pseudo-random, period
//             2^N – 1 (Fibonacci LFSR).  Benjolin white-noise character at high
//             clock rates.
//   rungler — (data > param) XOR MSB; short evolving loops whose rate of change
//             is set by the data/clock frequency ratio.  Benjolin Rungler model.
//   turing  — probabilistic bit flip; param=0 → frozen loop, param=1 → random,
//             intermediate → slowly drifting.  Turing Machine model.
//   open    — no feedback; data > 0.5 shifts through directly.  Clocked S&H
//             chain or externally-seeded pattern register.
//
// Parameters (via update()):
//   "clock_rate" — internal clock rate in Hz [0.001, 1000]  (default 2.0)
//   "data"       — data input [0, 1]                         (default 0.5)
//   "param"      — mode scalar: threshold / probability [0, 1] (default 0.5)
//   "depth"      — output scale [0, 1]                       (default 1.0)
//
// Accessors:
//   gate_out() — true on ticks where a clock edge fired and a 1-bit entered
//   state()    — full register word; expander-style consumers can read all bits
class shift_register_modulator final : public abstract_modulator {
public:
    enum class mode { lfsr, rungler, turing, open };

    explicit shift_register_modulator(mode m        = mode::turing,
                                      int  length   = 16,
                                      int  dac_bits = 3);

    float tick(double beat, float tick_rate_hz) override;
    void  update(std::string_view key, float value) override;

    bool     gate_out() const noexcept { return gate_out_; }
    uint32_t state()    const noexcept { return reg_; }

private:
    bool  lfsr_new_bit()   const noexcept;
    float dac_output()     const noexcept;
    float next_random()    noexcept;

    mode     mode_;
    int      length_;    // [2, 32]
    int      dac_bits_;  // [1, min(length_, 8)]

    uint32_t reg_;
    bool     gate_out_{false};

    float clock_rate_{2.0f};
    float data_{0.5f};
    float param_{0.5f};
    float depth_{1.0f};

    float    clock_phase_{0.0f};
    uint32_t rand_state_{0xACE1u};
};

} // namespace nomos::rt
