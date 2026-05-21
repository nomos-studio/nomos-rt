// SPDX-License-Identifier: LGPL-2.1-or-later
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "statues_modulator.hpp"

using nomos::rt::statues_modulator;

namespace {
constexpr float rate = 375.0f;
} // namespace

TEST_CASE("statues: default construction does not crash", "[statues]") {
    REQUIRE_NOTHROW(statues_modulator{});
}

TEST_CASE("statues: slots are zero-initialised", "[statues]") {
    statues_modulator m;
    const auto out = m.tick(0.0, rate);
    for (int i = 0; i < 8; ++i)
        REQUIRE(out.outputs[i] == Catch::Approx(0.0f));
}

TEST_CASE("statues: IN writes to addressed slot", "[statues]") {
    statues_modulator m;
    m.update("in", 0.5f);
    m.update("addr", 3.0f);
    const auto out = m.tick(0.0, rate);
    REQUIRE(out.outputs[3] == Catch::Approx(0.5f));
    REQUIRE(out.cv == Catch::Approx(0.5f));
}

TEST_CASE("statues: unaddressed slots retain previous values", "[statues]") {
    statues_modulator m;
    // Write 0.3 to slot 2.
    m.update("in", 0.3f);
    m.update("addr", 2.0f);
    m.tick(0.0, rate);

    // Move to slot 5.
    m.update("addr", 5.0f);
    m.update("in", -0.7f);
    const auto out = m.tick(0.0, rate);

    REQUIRE(out.outputs[2] == Catch::Approx(0.3f));   // still held
    REQUIRE(out.outputs[5] == Catch::Approx(-0.7f));  // just written
}

TEST_CASE("statues: all 8 slots independently addressable", "[statues]") {
    statues_modulator m;
    for (int i = 0; i < 8; ++i) {
        m.update("in",   static_cast<float>(i) * 0.1f);
        m.update("addr", static_cast<float>(i));
        m.tick(0.0, rate);
    }
    const auto out = m.tick(0.0, rate);
    for (int i = 0; i < 8; ++i)
        REQUIRE(out.outputs[i] == Catch::Approx(static_cast<float>(i) * 0.1f).margin(1e-5f));
}

TEST_CASE("statues: state field reports current address", "[statues]") {
    statues_modulator m;
    m.update("addr", 6.0f);
    const auto out = m.tick(0.0, rate);
    REQUIRE(out.state == 6u);
}

TEST_CASE("statues: gate fires when address changes", "[statues]") {
    statues_modulator m;
    m.update("addr", 0.0f);
    m.tick(0.0, rate);  // establish addr=0

    m.update("addr", 4.0f);
    auto out = m.tick(0.0, rate);
    REQUIRE(out.gate);  // address changed

    out = m.tick(0.0, rate);
    REQUIRE_FALSE(out.gate);  // no change
}

TEST_CASE("statues: addr bits select correct slot", "[statues]") {
    statues_modulator m;
    // addr = 5 = 0b101 → addr0=1, addr1=0, addr2=1
    m.update("addr0", 1.0f);
    m.update("addr1", 0.0f);
    m.update("addr2", 1.0f);
    m.update("in", 0.42f);
    const auto out = m.tick(0.0, rate);
    REQUIRE(out.outputs[5] == Catch::Approx(0.42f));
    REQUIRE(out.state == 5u);
}

TEST_CASE("statues: addr clamped to [0, 7]", "[statues]") {
    statues_modulator m;
    m.update("addr", 20.0f);
    REQUIRE_NOTHROW(m.tick(0.0, rate));
    m.update("addr", -5.0f);
    REQUIRE_NOTHROW(m.tick(0.0, rate));
}

TEST_CASE("statues: unknown update key is a no-op", "[statues]") {
    statues_modulator m;
    REQUIRE_NOTHROW(m.update("bogus", 1.0f));
    REQUIRE_NOTHROW(m.tick(0.0, rate));
}
