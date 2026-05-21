// SPDX-License-Identifier: LGPL-2.1-or-later
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "divine_cmos_modulator.hpp"

using nomos::rt::divine_cmos_modulator;

namespace {
constexpr float rate = 375.0f;
} // namespace

TEST_CASE("divine_cmos: default construction does not crash", "[divine-cmos]") {
    REQUIRE_NOTHROW(divine_cmos_modulator{});
}

TEST_CASE("divine_cmos: single-clock produces 4 division outputs", "[divine-cmos]") {
    divine_cmos_modulator m;
    // Clock 1 ticks; counter2=0 so XOR = counter1.
    // After 1 edge: counter1=1 → bits=0001 → out[0]=1, rest 0.
    m.update("clock1_tick", 1.0f);
    auto out = m.tick(0.0, rate);
    REQUIRE(out.gate);               // out[0] = ÷2 = 1
    REQUIRE_FALSE(out.gate2);        // out[1] = ÷4 = 0
    REQUIRE((out.state & 0x1u) != 0);
}

TEST_CASE("divine_cmos: ÷2 toggles every clock edge", "[divine-cmos]") {
    divine_cmos_modulator m;
    bool prev = false;
    int toggles = 0;
    for (int i = 0; i < 8; ++i) {
        m.update("clock1_tick", 1.0f);
        const auto out = m.tick(0.0, rate);
        if (out.gate != prev) { ++toggles; prev = out.gate; }
    }
    REQUIRE(toggles == 8);  // toggles every single edge
}

TEST_CASE("divine_cmos: ÷4 toggles every two clock edges", "[divine-cmos]") {
    divine_cmos_modulator m;
    bool prev = false;
    int toggles = 0;
    for (int i = 0; i < 16; ++i) {
        m.update("clock1_tick", 1.0f);
        const auto out = m.tick(0.0, rate);
        if (out.gate2 != prev) { ++toggles; prev = out.gate2; }
    }
    REQUIRE(toggles == 8);  // toggles every two edges
}

TEST_CASE("divine_cmos: MAIN is 0 when all gains are 0", "[divine-cmos]") {
    divine_cmos_modulator m;
    m.update("gain0", 0.0f); m.update("gain1", 0.0f);
    m.update("gain2", 0.0f); m.update("gain3", 0.0f);
    m.update("clock1_tick", 1.0f);
    const auto out = m.tick(0.0, rate);
    REQUIRE(out.cv == Catch::Approx(0.0f));
}

TEST_CASE("divine_cmos: MAIN in [0, 1] across many clocks", "[divine-cmos]") {
    divine_cmos_modulator m;
    for (int i = 0; i < 32; ++i) {
        m.update("clock1_tick", 1.0f);
        const auto out = m.tick(0.0, rate);
        REQUIRE(out.cv >= 0.0f);
        REQUIRE(out.cv <= 1.0f);
    }
}

TEST_CASE("divine_cmos: dual-clock XOR differs from single-clock", "[divine-cmos]") {
    divine_cmos_modulator single, dual;
    // Dual clock 2 ticks every other clock 1.
    bool saw_difference = false;
    for (int i = 0; i < 32; ++i) {
        single.update("clock1_tick", 1.0f);
        dual.update("clock1_tick", 1.0f);
        if (i % 2 == 0) dual.update("clock2_tick", 1.0f);
        const auto os = single.tick(0.0, rate);
        const auto od = dual.tick(0.0, rate);
        if (os.state != od.state) saw_difference = true;
    }
    REQUIRE(saw_difference);
}

TEST_CASE("divine_cmos: SLEW output lags MAIN", "[divine-cmos]") {
    divine_cmos_modulator m;
    m.update("slew", 0.5f);
    // Drive a step change: fire 8 clocks to set all bits.
    for (int i = 0; i < 8; ++i) {
        m.update("clock1_tick", 1.0f);
        m.tick(0.0, rate);
    }
    // Now get a tick with no new clock.
    const auto out = m.tick(0.0, rate);
    // SLEW should be strictly less than MAIN if there was a step up (or at most equal).
    REQUIRE(out.aux <= out.cv + 1e-4f);
}

TEST_CASE("divine_cmos: unknown update key is a no-op", "[divine-cmos]") {
    divine_cmos_modulator m;
    REQUIRE_NOTHROW(m.update("bogus", 1.0f));
    REQUIRE_NOTHROW(m.tick(0.0, rate));
}
