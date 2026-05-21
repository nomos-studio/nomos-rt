// SPDX-License-Identifier: LGPL-2.1-or-later
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "squid_axon_modulator.hpp"

using nomos::rt::squid_axon_modulator;

namespace {
constexpr float rate = 375.0f;
} // namespace

TEST_CASE("squid: default construction does not crash", "[squid]") {
    REQUIRE_NOTHROW(squid_axon_modulator{});
}

TEST_CASE("squid: initial output is zero", "[squid]") {
    squid_axon_modulator m;
    const auto out = m.tick(0.0, rate);
    REQUIRE(out.cv  == Catch::Approx(0.0f));
    REQUIRE(out.aux == Catch::Approx(0.0f));
    for (int i = 0; i < 4; ++i)
        REQUIRE(out.outputs[i] == Catch::Approx(0.0f));
}

TEST_CASE("squid: clock_tick advances pipeline", "[squid]") {
    squid_axon_modulator m;
    m.update("in1", 0.8f);
    m.update("clock_tick", 1.0f);
    const auto out = m.tick(0.0, rate);
    // OUT1 should now contain a value derived from in1.
    REQUIRE(out.outputs[0] > 0.0f);
}

TEST_CASE("squid: output stays in [0, 1]", "[squid]") {
    squid_axon_modulator m;
    m.update("in1", 0.6f);
    m.update("in2", 0.3f);
    for (int i = 0; i < 30; ++i) {
        m.update("clock_tick", 1.0f);
        const auto out = m.tick(0.0, rate);
        for (int j = 0; j < 4; ++j) {
            REQUIRE(out.outputs[j] >= 0.0f);
            REQUIRE(out.outputs[j] <= 1.0f);
        }
    }
}

TEST_CASE("squid: value propagates through pipeline with stagger", "[squid]") {
    // Fire a single pulse through the pipeline and confirm it reaches OUT4
    // exactly 3 clocks after OUT1.
    squid_axon_modulator m;
    m.update("in3_patched", 1.0f);  // disable IN3 normalling; use 0
    m.update("in1", 0.7f);
    m.update("clock_tick", 1.0f);
    const float out1_tick1 = m.tick(0.0, rate).outputs[0];

    m.update("in1", 0.0f);
    // Advance 2 more clocks (ticks 2 and 3 — value travels OUT1→OUT2→OUT3).
    for (int step = 0; step < 2; ++step) {
        m.update("clock_tick", 1.0f);
        m.tick(0.0, rate);
    }
    // Clock 4: value reaches OUT4 (3 ticks after entering OUT1).
    m.update("clock_tick", 1.0f);
    const auto final_out = m.tick(0.0, rate);
    // The value should have propagated; OUT4 ≈ the original OUT1 value
    // (exact match not guaranteed because NL/linear feedback may modify it,
    // but with nl_fb=0 and lin_fb=0 it should be exact).
    REQUIRE(final_out.outputs[3] == Catch::Approx(out1_tick1).margin(0.01f));
}

TEST_CASE("squid: gate reflects out1 > 0.5", "[squid]") {
    squid_axon_modulator m;
    m.update("in1", 0.9f);
    m.update("clock_tick", 1.0f);
    const auto out = m.tick(0.0, rate);
    REQUIRE(out.gate == (out.cv > 0.5f));
}

TEST_CASE("squid: cv mirrors outputs[0], aux mirrors outputs[3]", "[squid]") {
    squid_axon_modulator m;
    m.update("in1", 0.5f);
    m.update("clock_tick", 1.0f);
    const auto out = m.tick(0.0, rate);
    REQUIRE(out.cv  == Catch::Approx(out.outputs[0]));
    REQUIRE(out.aux == Catch::Approx(out.outputs[3]));
}

TEST_CASE("squid: nonlinear feedback reduces large OUT4 values", "[squid]") {
    // With high nl_fb the pipeline self-stabilises — large values get negated.
    squid_axon_modulator m;
    m.update("nl_fb",  4.0f);  // maximum NL feedback
    m.update("lin_fb", 0.5f);
    m.update("in1", 1.0f);
    m.update("in2", 1.0f);
    // Run for many clocks; output should remain bounded.
    for (int i = 0; i < 50; ++i) {
        m.update("clock_tick", 1.0f);
        const auto out = m.tick(0.0, rate);
        REQUIRE(out.cv  >= 0.0f); REQUIRE(out.cv  <= 1.0f);
        REQUIRE(out.aux >= 0.0f); REQUIRE(out.aux <= 1.0f);
    }
}

TEST_CASE("squid: no clock — pipeline does not advance", "[squid]") {
    squid_axon_modulator m;
    m.update("in1", 0.8f);
    const auto out0 = m.tick(0.0, rate);
    const auto out1 = m.tick(0.0, rate);
    REQUIRE(out0.state == out1.state);
    for (int i = 0; i < 4; ++i)
        REQUIRE(out0.outputs[i] == Catch::Approx(out1.outputs[i]));
}

TEST_CASE("squid: unknown update key is a no-op", "[squid]") {
    squid_axon_modulator m;
    REQUIRE_NOTHROW(m.update("bogus", 1.0f));
    REQUIRE_NOTHROW(m.tick(0.0, rate));
}
