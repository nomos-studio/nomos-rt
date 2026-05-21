// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <nomos/rt/abstract_modulator.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace nomos::rt {

class modulator_engine;

// 8-bit shift register with STROBE output-freeze and weighted 4-CV projection.
//
// On each rising edge of the clock input the register shifts left by one bit.
// The new LSB is determined by the feedback mode:
//   data2 absent: bit_in = (bit7 XOR bit0) | data1   (pseudo-random loop)
//   data2 present: bit_in = data1 | data2             (open mode; no self-feedback)
//
// STROBE decoupling: when STROBE is low the output register mirrors the internal
// register (standard operation). When STROBE is high the output register freezes
// while the internal register continues shifting — the pattern evolves internally
// and is revealed at the next STROBE falling edge.
//
// Weighted 4-CV projection from the 8-bit output register:
//   cv1 ← bits 0, 3, 4, 7
//   cv2 ← bits 0, 1, 4, 5
//   cv3 ← bits 1, 2, 5, 6
//   cv4 ← bits 2, 3, 6, 7
// Each bit's contribution follows a non-uniform weight ladder [0.05, 0.11, 0.25, 0.53]
// in ascending index order within the contributing set.  Output normalised to [0, 1].
//
// SERIAL output: the bit shifted out of position 7 on each clock edge (useful for
// chaining two cipher instances).
//
// Outputs (modulator_output):
//   .cv        — cv1 [0, 1]
//   .aux       — cv2 [0, 1]
//   .gate      — SERIAL output (bit shifted out of MSB)
//   .gate2     — STROBE active (output currently frozen)
//   .state     — output_reg (8-bit pattern; bit N = gate output N)
//   .outputs[0..3] — cv1, cv2, cv3, cv4
//
// Parameters (via update()):
//   "data1"        — primary data input (>0.5 = high)          (default 0.0)
//   "data2"        — secondary data input; enables open mode    (default -1 = absent)
//   "strobe"       — output freeze level (>0.5 = freeze)       (default 0.0)
//   "clock_tick"   — >0.5 arms one clock edge (one-shot)
//   "data2_enable" — >0.5 enables data2 input (use when no source_id for data2)
class cipher_modulator final : public abstract_modulator {
public:
    // Source IDs for clock, data1, data2, strobe — empty = use update() parameters.
    explicit cipher_modulator(const modulator_engine* engine      = nullptr,
                              std::string             clock_src   = {},
                              std::string             data1_src   = {},
                              std::string             data2_src   = {},
                              std::string             strobe_src  = {});

    modulator_output tick(double beat, float tick_rate_hz) override;
    void             update(std::string_view key, float value) override;

private:
    float project_cv(int cv_idx) const noexcept;
    bool  read_gate_src(const std::string& src_id) const noexcept;
    bool  read_gate_level(const std::string& src_id, float fallback) const noexcept;

    const modulator_engine* engine_;
    std::string             clock_src_;
    std::string             data1_src_;
    std::string             data2_src_;
    std::string             strobe_src_;

    uint8_t inner_reg_{0};   // shifts freely every clock
    uint8_t output_reg_{0};  // frozen when strobe is high

    bool clock_prev_{false};
    bool clock_pending_{false};
    bool serial_out_{false};

    float data1_{0.0f};
    float data2_{-1.0f};    // < 0 = data2 absent (XOR feedback mode)
    float strobe_{0.0f};

    // Weight ladder for CV projection (ascending within each contributing set).
    static constexpr float kWeights[4] = {0.05f, 0.11f, 0.25f, 0.53f};
    static constexpr float kWeightSum  = 0.05f + 0.11f + 0.25f + 0.53f;

    // Bit indices contributing to each CV output.
    static constexpr int kCVBits[4][4] = {
        {0, 3, 4, 7},  // cv1
        {0, 1, 4, 5},  // cv2
        {1, 2, 5, 6},  // cv3
        {2, 3, 6, 7},  // cv4
    };
};

} // namespace nomos::rt
