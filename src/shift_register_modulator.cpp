// SPDX-License-Identifier: LGPL-2.1-or-later
#include "shift_register_modulator.hpp"

#include <algorithm>
#include <cmath>

namespace nomos::rt {

// ---------------------------------------------------------------------------
// Fibonacci LFSR tap masks for maximal-length sequences, 2–32 bits.
//
// Convention: left-shifting register; new bit enters at bit 0 (LSB); output
// at bit N-1 (MSB).  Feedback bit = XOR-parity of (reg & taps[N]).
//
// Sources: standard polynomial tables; 8-bit (0xB8) and 16-bit (0xD008) are
// the Benjolin and Turing Machine register lengths respectively.
// ---------------------------------------------------------------------------
static constexpr uint32_t lfsr_taps[33] = {
    0x00000000, // 0  — unused
    0x00000000, // 1  — degenerate
    0x00000003, // 2  x^2+x+1
    0x00000006, // 3  x^3+x^2+1
    0x0000000C, // 4  x^4+x^3+1
    0x00000014, // 5  x^5+x^3+1
    0x00000030, // 6  x^6+x^5+1
    0x00000060, // 7  x^7+x^6+1
    0x000000B8, // 8  x^8+x^6+x^5+x^4+1  (Benjolin)
    0x00000110, // 9  x^9+x^5+1
    0x00000240, // 10 x^10+x^7+1
    0x00000500, // 11 x^11+x^9+1
    0x00000E08, // 12 x^12+x^11+x^10+x^4+1
    0x00001C80, // 13 x^13+x^12+x^11+x^8+1
    0x00003802, // 14 x^14+x^13+x^12+x^2+1
    0x00006000, // 15 x^15+x^14+1
    0x0000D008, // 16 x^16+x^15+x^13+x^4+1  (Turing Machine)
    0x00012000, // 17 x^17+x^14+1
    0x00020400, // 18 x^18+x^11+1
    0x00072000, // 19 x^19+x^18+x^17+x^14+1
    0x00090000, // 20 x^20+x^17+1
    0x00140000, // 21 x^21+x^19+1
    0x00300000, // 22 x^22+x^21+1
    0x00420000, // 23 x^23+x^18+1
    0x00E10000, // 24 x^24+x^23+x^22+x^17+1
    0x01200000, // 25 x^25+x^22+1
    0x02000023, // 26 x^26+x^6+x^2+x+1
    0x04000013, // 27 x^27+x^5+x^2+x+1
    0x09000000, // 28 x^28+x^25+1
    0x14000000, // 29 x^29+x^27+1
    0x20000029, // 30 x^30+x^6+x^4+x+1
    0x48000000, // 31 x^31+x^28+1
    0x80200003, // 32 x^32+x^22+x^2+x+1
};

shift_register_modulator::shift_register_modulator(mode m, int length, int dac_bits)
    : mode_(m)
    , length_(std::clamp(length, 2, 32))
    , dac_bits_(std::clamp(dac_bits, 1, std::min(length_, 8)))
    , reg_(0xACE1u & ((1u << length_) - 1u))
{
    if (reg_ == 0u)
        reg_ = 1u;  // LFSR must be non-zero to produce any sequence
}

modulator_output shift_register_modulator::tick(double /*beat*/, float tick_rate_hz) {
    if (tick_rate_hz <= 0.0f)
        return {.cv = dac_output() * depth_, .state = reg_};

    clock_phase_ += clock_rate_ / tick_rate_hz;

    if (clock_phase_ < 1.0f)
        return {.cv = dac_output() * depth_, .state = reg_};

    clock_phase_ -= 1.0f;

    bool new_bit;
    switch (mode_) {
        case mode::lfsr:
            new_bit = lfsr_new_bit();
            break;
        case mode::rungler: {
            const bool msb = (reg_ >> (length_ - 1)) & 1u;
            new_bit = (data_ > param_) ^ msb;
            break;
        }
        case mode::turing: {
            const bool msb = (reg_ >> (length_ - 1)) & 1u;
            new_bit = (next_random() < param_) ? !msb : msb;
            break;
        }
        case mode::open:
            new_bit = data_ > 0.5f;
            break;
    }

    const uint32_t mask = (length_ < 32) ? ((1u << length_) - 1u) : 0xFFFF'FFFFu;
    reg_ = ((reg_ << 1u) | (new_bit ? 1u : 0u)) & mask;
    gate_out_ = new_bit;

    return {.cv = dac_output() * depth_, .gate = gate_out_, .state = reg_};
}

void shift_register_modulator::update(std::string_view key, float value) {
    if (key == "clock_rate")
        clock_rate_ = std::clamp(value, 0.001f, 1000.0f);
    else if (key == "data")
        data_ = std::clamp(value, 0.0f, 1.0f);
    else if (key == "param")
        param_ = std::clamp(value, 0.0f, 1.0f);
    else if (key == "depth")
        depth_ = std::clamp(value, 0.0f, 1.0f);
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

bool shift_register_modulator::lfsr_new_bit() const noexcept {
    // XOR-parity of all bits selected by the tap mask.
    uint32_t x = reg_ & lfsr_taps[static_cast<std::size_t>(length_)];
    x ^= x >> 16;
    x ^= x >> 8;
    x ^= x >> 4;
    x ^= x >> 2;
    x ^= x >> 1;
    return x & 1u;
}

float shift_register_modulator::dac_output() const noexcept {
    // Read dac_bits_ most-significant bits of the register.
    const int    shift   = length_ - dac_bits_;
    const uint32_t raw   = (reg_ >> shift) & ((1u << dac_bits_) - 1u);
    const float  max_val = static_cast<float>((1u << dac_bits_) - 1u);
    return (static_cast<float>(raw) / max_val) * 2.0f - 1.0f;
}

float shift_register_modulator::next_random() noexcept {
    // Park-Miller LCG, period ~2^31.
    rand_state_ = rand_state_ * 1664525u + 1013904223u;
    return static_cast<float>(rand_state_ >> 8) * (1.0f / 16777216.0f);
}

} // namespace nomos::rt
