// SPDX-License-Identifier: LGPL-2.1-or-later
#include "statues_modulator.hpp"

#include <nomos/rt/modulator_engine.hpp>

#include <algorithm>
#include <cmath>

namespace nomos::rt {

statues_modulator::statues_modulator(const modulator_engine* engine,
                                     std::string in_source_id,
                                     std::string addr_source_id)
    : engine_(engine)
    , in_source_id_(std::move(in_source_id))
    , addr_source_id_(std::move(addr_source_id))
{}

modulator_output statues_modulator::tick(double /*beat*/, float /*tick_rate_hz*/) {
    // Resolve IN value.
    float in_val = in_;
    if (engine_ && !in_source_id_.empty()) {
        const auto* out = engine_->last_output(in_source_id_);
        if (out) in_val = out->cv;
    }

    // Resolve address.
    int addr = addr_;
    if (engine_ && !addr_source_id_.empty()) {
        const auto* out = engine_->last_output(addr_source_id_);
        if (out) addr = static_cast<int>(out->state) & 0x7;
    } else if (use_addr_bits_) {
        addr = (addr_bits_[0] ? 1 : 0)
             | (addr_bits_[1] ? 2 : 0)
             | (addr_bits_[2] ? 4 : 0);
    }
    addr = std::clamp(addr, 0, 7);

    // Write current IN to addressed slot.
    slots_[addr] = in_val;

    const bool addr_changed = (addr != prev_addr_);
    prev_addr_ = addr;

    modulator_output out;
    out.cv    = slots_[addr];
    out.aux   = in_val;
    out.gate  = addr_changed;
    out.state = static_cast<uint32_t>(addr);
    for (int i = 0; i < 8; ++i)
        out.outputs[i] = slots_[i];
    return out;
}

void statues_modulator::update(std::string_view key, float value) {
    if      (key == "in")    in_   = std::clamp(value, -1.0f, 1.0f);
    else if (key == "addr") { addr_ = std::clamp(static_cast<int>(std::round(value)), 0, 7);
                              use_addr_bits_ = false; }
    else if (key == "addr0") { addr_bits_[0] = value > 0.5f; use_addr_bits_ = true; }
    else if (key == "addr1") { addr_bits_[1] = value > 0.5f; use_addr_bits_ = true; }
    else if (key == "addr2") { addr_bits_[2] = value > 0.5f; use_addr_bits_ = true; }
}

} // namespace nomos::rt
