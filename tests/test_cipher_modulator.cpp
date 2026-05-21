// SPDX-License-Identifier: LGPL-2.1-or-later
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "cipher_modulator.hpp"

#include <set>

using nomos::rt::cipher_modulator;

namespace {
constexpr float rate = 375.0f;
} // namespace

TEST_CASE("cipher: default construction does not crash", "[cipher]") {
    REQUIRE_NOTHROW(cipher_modulator{});
}

TEST_CASE("cipher: initial output is zero", "[cipher]") {
    cipher_modulator m;
    const auto out = m.tick(0.0, rate);
    REQUIRE(out.cv  == Catch::Approx(0.0f));
    REQUIRE(out.aux == Catch::Approx(0.0f));
    REQUIRE(out.state == 0u);
}

TEST_CASE("cipher: CV outputs in [0, 1]", "[cipher]") {
    cipher_modulator m;
    m.update("data1", 1.0f);
    for (int i = 0; i < 100; ++i) {
        m.update("clock_tick", 1.0f);
        const auto out = m.tick(0.0, rate);
        REQUIRE(out.cv  >= 0.0f); REQUIRE(out.cv  <= 1.0f);
        REQUIRE(out.aux >= 0.0f); REQUIRE(out.aux <= 1.0f);
        for (int j = 0; j < 4; ++j) {
            REQUIRE(out.outputs[j] >= 0.0f);
            REQUIRE(out.outputs[j] <= 1.0f);
        }
    }
}

TEST_CASE("cipher: cv field mirrors outputs[0]", "[cipher]") {
    cipher_modulator m;
    m.update("data1", 1.0f);
    for (int i = 0; i < 20; ++i) {
        m.update("clock_tick", 1.0f);
        const auto out = m.tick(0.0, rate);
        REQUIRE(out.cv == Catch::Approx(out.outputs[0]));
        REQUIRE(out.aux == Catch::Approx(out.outputs[1]));
    }
}

TEST_CASE("cipher: register advances only on clock edges", "[cipher]") {
    cipher_modulator m;
    m.update("data1", 1.0f);
    // Two ticks with no clock — register must not change.
    const auto out0 = m.tick(0.0, rate);
    const auto out1 = m.tick(0.0, rate);
    REQUIRE(out0.state == out1.state);
}

TEST_CASE("cipher: register shifts on clock_tick", "[cipher]") {
    cipher_modulator m;
    m.update("data1", 1.0f);
    m.update("clock_tick", 1.0f);
    m.tick(0.0, rate);  // clock edge fires

    m.update("clock_tick", 1.0f);
    const auto out = m.tick(0.0, rate);
    // With data1=1: bit_in = feedback | 1 = 1 always → register fills with 1s.
    REQUIRE(out.state != 0u);
}

TEST_CASE("cipher: STROBE freezes output register", "[cipher]") {
    cipher_modulator m;
    m.update("data1", 1.0f);

    // Advance without strobe — capture state.
    m.update("clock_tick", 1.0f);
    const uint8_t before = static_cast<uint8_t>(m.tick(0.0, rate).state);

    // Enable strobe.
    m.update("strobe", 1.0f);
    // Clock advances inner register, but output should stay frozen.
    for (int i = 0; i < 8; ++i) {
        m.update("clock_tick", 1.0f);
        const auto out = m.tick(0.0, rate);
        REQUIRE(out.state == before);  // output frozen
        REQUIRE(out.gate2);            // strobe active
    }

    // Release strobe — output should follow inner register.
    m.update("strobe", 0.0f);
    const auto out_after = m.tick(0.0, rate);
    REQUIRE_FALSE(out_after.gate2);
}

TEST_CASE("cipher: open mode (data2 enabled) disables XOR feedback", "[cipher]") {
    // With data1=0, data2=0 in open mode: all zeros enter, register stays 0.
    cipher_modulator m;
    m.update("data2_enable", 1.0f);  // enable open mode
    m.update("data1", 0.0f);
    m.update("data2", 0.0f);
    for (int i = 0; i < 16; ++i) {
        m.update("clock_tick", 1.0f);
        REQUIRE(m.tick(0.0, rate).state == 0u);
    }
}

TEST_CASE("cipher: XOR feedback produces variation from constant data1=0", "[cipher]") {
    // data1=0 in XOR mode: only XOR(bit7, bit0) drives the register.
    // Starting from non-zero register should produce a pattern.
    cipher_modulator m;
    // Seed the register with data1=1 for a few clocks.
    m.update("data1", 1.0f);
    for (int i = 0; i < 4; ++i) { m.update("clock_tick", 1.0f); m.tick(0.0, rate); }

    // Now data1=0 — register should still evolve via XOR feedback.
    m.update("data1", 0.0f);
    std::set<uint8_t> seen;
    for (int i = 0; i < 32; ++i) {
        m.update("clock_tick", 1.0f);
        seen.insert(static_cast<uint8_t>(m.tick(0.0, rate).state));
    }
    REQUIRE(seen.size() > 1u);
}

TEST_CASE("cipher: 4 CV outputs are distinct for non-trivial registers", "[cipher]") {
    // Use open mode to write alternating bits directly (no XOR feedback blurring).
    // 8 clocks with data1=1,0,1,0,1,0,1,0 in open mode → register = 0xAA (10101010).
    // The four CV slices hit different bit positions, producing unequal values.
    cipher_modulator m;
    m.update("data2_enable", 1.0f);  // open mode: bit_in = data1
    for (int i = 0; i < 8; ++i) {
        m.update("data1", (i % 2 == 0) ? 1.0f : 0.0f);
        m.update("clock_tick", 1.0f);
        m.tick(0.0, rate);
    }
    const auto out = m.tick(0.0, rate);
    bool all_same = (out.outputs[0] == out.outputs[1] &&
                     out.outputs[1] == out.outputs[2] &&
                     out.outputs[2] == out.outputs[3]);
    REQUIRE_FALSE(all_same);
}

TEST_CASE("cipher: unknown update key is a no-op", "[cipher]") {
    cipher_modulator m;
    REQUIRE_NOTHROW(m.update("bogus", 1.0f));
    REQUIRE_NOTHROW(m.tick(0.0, rate));
}
