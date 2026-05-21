// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <nomos/rt/abstract_modulator.hpp>

#include <string>
#include <string_view>

namespace nomos::rt {

class modulator_engine;

// 3-bit addressed demultiplexing sample-and-hold with 8 independently held outputs.
//
// Three address bits (addr0/addr1/addr2) form a 3-bit binary address (0–7).
// On each tick, the input value is written to the addressed slot. All other
// slots hold their last written value indefinitely — outputs are persistent,
// never reset to zero on deselection.
//
// The address can be driven from a cross-modulator source: pass addr_source_id
// at construction to read the low 3 bits of engine->last_output(id).state as the
// address. Individual address bits can also be set via update("addr0"/"addr1"/"addr2").
// A direct integer address is settable via update("addr", value).
//
// Outputs (modulator_output):
//   .cv        — value of the currently addressed slot
//   .aux       — raw IN value (passthrough)
//   .gate      — true for one tick when the address changed (new slot selected)
//   .state     — current address (0–7)
//   .outputs[] — all 8 held slot values: outputs[0..7]
//
// Parameters (via update()):
//   "in"    — input signal [-1, 1]         (default 0.0; ignored when in_source_id set)
//   "addr"  — address [0, 7]               (rounded to integer; ignored when addr_source_id set)
//   "addr0" — address bit 0 (>0.5 = high)
//   "addr1" — address bit 1 (>0.5 = high)
//   "addr2" — address bit 2 (>0.5 = high)
class statues_modulator final : public abstract_modulator {
public:
    // in_source_id   — if non-empty, reads engine->last_output(id).cv for IN
    // addr_source_id — if non-empty, reads low 3 bits of engine->last_output(id).state
    explicit statues_modulator(const modulator_engine* engine        = nullptr,
                               std::string             in_source_id  = {},
                               std::string             addr_source_id = {});

    modulator_output tick(double beat, float tick_rate_hz) override;
    void             update(std::string_view key, float value) override;

private:
    const modulator_engine* engine_;
    std::string             in_source_id_;
    std::string             addr_source_id_;

    float slots_[8]{};

    float in_{0.0f};
    int   addr_{0};
    bool  addr_bits_[3]{};
    bool  use_addr_bits_{false};

    int   prev_addr_{-1};
};

} // namespace nomos::rt
