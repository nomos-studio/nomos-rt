// SPDX-License-Identifier: LGPL-2.1-or-later
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "bools_ring_modulator.hpp"

using nomos::rt::bools_ring_modulator;
using mode = bools_ring_modulator::mode;

namespace {
constexpr float rate = 375.0f;
constexpr float kStepMax = 0.55f + 1.09f + 2.19f + 4.37f;  // 8.20
} // namespace

TEST_CASE("bools_ring: default construction does not crash", "[bools-ring]") {
    REQUIRE_NOTHROW(bools_ring_modulator{});
}

TEST_CASE("bools_ring: all-zero inputs produce zero STEP in XOR mode", "[bools-ring]") {
    bools_ring_modulator m;
    // XOR(0,0)=0 for all pairs → bitmap=0, STEP=0.
    const auto out = m.tick(0.0, rate);
    REQUIRE(out.cv    == Catch::Approx(0.0f));
    REQUIRE(out.state == 0u);
    REQUIRE_FALSE(out.gate);
    REQUIRE_FALSE(out.gate2);
}

TEST_CASE("bools_ring: XOR ring — only in0 high sets out[0] and out[3]", "[bools-ring]") {
    // in = {1,0,0,0}
    // out[0] = XOR(in0,in1) = XOR(1,0) = 1
    // out[1] = XOR(in1,in2) = XOR(0,0) = 0
    // out[2] = XOR(in2,in3) = XOR(0,0) = 0
    // out[3] = XOR(in3,in0) = XOR(0,1) = 1  ← ring wrap
    bools_ring_modulator m;
    m.update("in0", 1.0f);
    const auto out = m.tick(0.0, rate);
    REQUIRE(out.state == 0b1001u);
    REQUIRE(out.gate);             // out[0]
    REQUIRE_FALSE(out.gate2);     // out[1]
    const float expected = (0.55f + 4.37f) / kStepMax;
    REQUIRE(out.cv == Catch::Approx(expected).epsilon(1e-4f));
}

TEST_CASE("bools_ring: XOR of all-ones is all-zero", "[bools-ring]") {
    bools_ring_modulator m;
    m.update("in0", 1.0f); m.update("in1", 1.0f);
    m.update("in2", 1.0f); m.update("in3", 1.0f);
    const auto out = m.tick(0.0, rate);
    REQUIRE(out.state == 0u);
    REQUIRE(out.cv == Catch::Approx(0.0f));
}

TEST_CASE("bools_ring: OR mode — all-ones gives all-ones bitmap", "[bools-ring]") {
    bools_ring_modulator m(mode::or_mode);
    m.update("in0", 1.0f); m.update("in1", 1.0f);
    m.update("in2", 1.0f); m.update("in3", 1.0f);
    const auto out = m.tick(0.0, rate);
    REQUIRE(out.state == 0b1111u);
    REQUIRE(out.cv == Catch::Approx(1.0f));
}

TEST_CASE("bools_ring: AND mode — mixed inputs only set adjacent-same pairs", "[bools-ring]") {
    // in = {1,1,0,0}
    // out[0] = AND(1,1) = 1
    // out[1] = AND(1,0) = 0
    // out[2] = AND(0,0) = 0  (actually AND(in2,in3) = AND(0,0) = 0... wait)
    // out[3] = AND(0,1) = 0  ← ring wrap
    bools_ring_modulator m(mode::and_mode);
    m.update("in0", 1.0f); m.update("in1", 1.0f);
    const auto out = m.tick(0.0, rate);
    REQUIRE(out.state == 0b0001u);
    REQUIRE(out.gate);
}

TEST_CASE("bools_ring: NOR mode — all-zero inputs yield all-ones bitmap", "[bools-ring]") {
    bools_ring_modulator m(mode::nor_mode);
    const auto out = m.tick(0.0, rate);
    REQUIRE(out.state == 0b1111u);
    REQUIRE(out.cv == Catch::Approx(1.0f));
}

TEST_CASE("bools_ring: NAND mode — all-ones inputs yield all-zeros bitmap", "[bools-ring]") {
    bools_ring_modulator m(mode::nand_mode);
    m.update("in0", 1.0f); m.update("in1", 1.0f);
    m.update("in2", 1.0f); m.update("in3", 1.0f);
    const auto out = m.tick(0.0, rate);
    REQUIRE(out.state == 0u);
}

TEST_CASE("bools_ring: XNOR mode — all-same inputs yield all-ones bitmap", "[bools-ring]") {
    bools_ring_modulator m(mode::xnor_mode);
    // All false: XNOR(0,0)=1 for all pairs.
    auto out = m.tick(0.0, rate);
    REQUIRE(out.state == 0b1111u);
    // All true: XNOR(1,1)=1 for all pairs.
    m.update("in0", 1.0f); m.update("in1", 1.0f);
    m.update("in2", 1.0f); m.update("in3", 1.0f);
    out = m.tick(0.0, rate);
    REQUIRE(out.state == 0b1111u);
}

TEST_CASE("bools_ring: STEP CV is always in [0, 1]", "[bools-ring]") {
    // Sweep through all 16 input combinations in XOR mode.
    for (int bits = 0; bits < 16; ++bits) {
        bools_ring_modulator m;
        for (int i = 0; i < 4; ++i)
            m.update(std::string("in") + char('0' + i), (bits >> i) & 1 ? 1.0f : 0.0f);
        const auto out = m.tick(0.0, rate);
        REQUIRE(out.cv >= 0.0f);
        REQUIRE(out.cv <= 1.0f);
    }
}

TEST_CASE("bools_ring: gate/gate2 mirror out[0]/out[1]", "[bools-ring]") {
    // in={1,0,0,0}: out[0]=1, out[1]=0 as established above.
    bools_ring_modulator m;
    m.update("in0", 1.0f);
    const auto out = m.tick(0.0, rate);
    REQUIRE(out.gate  == ((out.state & 0x1u) != 0));
    REQUIRE(out.gate2 == ((out.state & 0x2u) != 0));
}

TEST_CASE("bools_ring: SLEW lags STEP on step-up", "[bools-ring]") {
    bools_ring_modulator m;
    m.update("slew", 0.5f);
    // One tick at zero → slew_state starts at 0.
    m.tick(0.0, rate);

    // Now drive all inputs high (bitmap=0 in XOR, since XOR(1,1)=0, STEP stays 0).
    // Use OR mode to get STEP=1.
    bools_ring_modulator m2(mode::or_mode);
    m2.update("slew", 0.5f);
    m2.tick(0.0, rate);  // initial tick, STEP=0

    // Drive a step: all inputs high → STEP=1.
    m2.update("in0", 1.0f); m2.update("in1", 1.0f);
    m2.update("in2", 1.0f); m2.update("in3", 1.0f);
    const auto out = m2.tick(0.0, rate);
    // STEP should be 1.0; SLEW should be strictly less (IIR hasn't caught up).
    REQUIRE(out.cv == Catch::Approx(1.0f));
    REQUIRE(out.aux < out.cv - 1e-4f);
}

TEST_CASE("bools_ring: SLEW=0 tracks STEP instantly", "[bools-ring]") {
    bools_ring_modulator m(mode::or_mode);
    m.update("in0", 1.0f); m.update("in1", 1.0f);
    m.update("in2", 1.0f); m.update("in3", 1.0f);
    const auto out = m.tick(0.0, rate);
    REQUIRE(out.aux == Catch::Approx(out.cv));
}

TEST_CASE("bools_ring: sample_tick freezes then releases output", "[bools-ring]") {
    bools_ring_modulator m;
    m.update("sampled", 1.0f);

    // With no sample edge, latch stays at initial (0).
    m.update("in0", 1.0f);
    const auto out_frozen = m.tick(0.0, rate);
    // latched_out_ = 0 (no edge yet) → bitmap stays 0.
    REQUIRE(out_frozen.state == 0u);

    // Fire sample_tick — arms the one-shot edge.
    m.update("sample_tick", 1.0f);
    const auto out_latched = m.tick(0.0, rate);
    // Now latched_out_ = current ring output (in0=1 → bitmap=0b1001).
    REQUIRE(out_latched.state == 0b1001u);

    // Another tick with no sample_tick — stays latched.
    const auto out_held = m.tick(0.0, rate);
    REQUIRE(out_held.state == 0b1001u);
}

TEST_CASE("bools_ring: unknown update key is a no-op", "[bools-ring]") {
    bools_ring_modulator m;
    REQUIRE_NOTHROW(m.update("bogus", 1.0f));
    REQUIRE_NOTHROW(m.tick(0.0, rate));
}
