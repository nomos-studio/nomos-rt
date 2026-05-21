// SPDX-License-Identifier: LGPL-2.1-or-later
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "lets_splosh_modulator.hpp"

using nomos::rt::lets_splosh_modulator;

namespace {
constexpr float rate = 375.0f;
} // namespace

TEST_CASE("splosh: default construction does not crash", "[splosh]") {
    REQUIRE_NOTHROW(lets_splosh_modulator{});
}

TEST_CASE("splosh: all-zero inputs produce all-zero outputs", "[splosh]") {
    lets_splosh_modulator m;
    const auto out = m.tick(0.0, rate);
    for (int i = 0; i < 16; ++i)
        REQUIRE(out.outputs[i] == Catch::Approx(0.0f));
    REQUIRE(out.cv  == Catch::Approx(0.0f));
    REQUIRE(out.aux == Catch::Approx(0.0f));
    REQUIRE_FALSE(out.gate);
}

TEST_CASE("splosh: outputs are always non-negative (half-wave rectified)", "[splosh]") {
    lets_splosh_modulator m;
    m.update("c", 0.8f); m.update("t", 0.3f);
    m.update("n", 0.6f); m.update("b", 0.1f);
    const auto out = m.tick(0.0, rate);
    for (int i = 0; i < 16; ++i)
        REQUIRE(out.outputs[i] >= 0.0f);
}

TEST_CASE("splosh: mask 1111 = max(0, C+T+N+B)", "[splosh]") {
    lets_splosh_modulator m;
    const float c=0.8f, t=0.3f, n=0.6f, b=0.1f;
    m.update("c", c); m.update("t", t);
    m.update("n", n); m.update("b", b);
    const auto out = m.tick(0.0, rate);
    const float expected = std::max(0.0f, c + t + n + b);
    REQUIRE(out.outputs[15] == Catch::Approx(expected).epsilon(1e-5f));
    REQUIRE(out.aux == Catch::Approx(expected).epsilon(1e-5f));
}

TEST_CASE("splosh: mask 0000 is always zero", "[splosh]") {
    lets_splosh_modulator m;
    m.update("c", 0.9f); m.update("t", 0.9f);
    m.update("n", 0.9f); m.update("b", 0.9f);
    const auto out = m.tick(0.0, rate);
    REQUIRE(out.outputs[0] == Catch::Approx(0.0f));
}

TEST_CASE("splosh: mask m and complement ~m cannot both be non-zero", "[splosh]") {
    lets_splosh_modulator m;
    m.update("c", 0.7f); m.update("t", 0.2f);
    m.update("n", 0.5f); m.update("b", 0.4f);
    const auto out = m.tick(0.0, rate);
    for (int mask = 1; mask < 15; ++mask) {
        const int comp = (~mask) & 0xF;
        REQUIRE_FALSE((out.outputs[mask] > 0.0f && out.outputs[comp] > 0.0f));
    }
}

TEST_CASE("splosh: single dominant input sets expected partitions", "[splosh]") {
    // C=1, T=N=B=0: only partitions where C is the sole positive member,
    // or where C's group beats the zero negative group, win.
    // mask 0001 (C alone vs T+N+B=0): out = max(0, 1-0) = 1
    // mask 0011 (C+T=1 vs N+B=0): out = 1, etc.
    // mask 1110 (T+N+B=0 vs C=1): out = 0
    lets_splosh_modulator m;
    m.update("c", 1.0f);
    const auto out = m.tick(0.0, rate);
    // All masks with bit 0 (C) set should equal 1.0 (pos_sum=1, neg_sum=0).
    for (int mask = 1; mask < 16; ++mask) {
        if ((mask & 0x1) && !(mask & ~0x1)) {
            // Only C in positive group, rest zero.
            REQUIRE(out.outputs[mask] == Catch::Approx(1.0f));
        }
    }
    // mask 0001 specifically.
    REQUIRE(out.outputs[0x1] == Catch::Approx(1.0f));
    // mask 1110 = T+N+B vs C → max(0, 0-1) = 0.
    REQUIRE(out.outputs[0xE] == Catch::Approx(0.0f));
}

TEST_CASE("splosh: cv is outputs[0x7] = max(0, C+T+N-B)", "[splosh]") {
    lets_splosh_modulator m;
    const float c=0.5f, t=0.4f, n=0.3f, b=0.8f;
    m.update("c", c); m.update("t", t);
    m.update("n", n); m.update("b", b);
    const auto out = m.tick(0.0, rate);
    const float expected = std::max(0.0f, c + t + n - b);
    REQUIRE(out.cv == Catch::Approx(expected).epsilon(1e-5f));
}

TEST_CASE("splosh: state bitmap reflects inputs > 0.5", "[splosh]") {
    lets_splosh_modulator m;
    m.update("c", 0.8f);  // above
    m.update("t", 0.3f);  // below
    m.update("n", 0.9f);  // above
    m.update("b", 0.1f);  // below
    const auto out = m.tick(0.0, rate);
    REQUIRE((out.state & 0x1u) != 0u);  // C above
    REQUIRE((out.state & 0x2u) == 0u);  // T below
    REQUIRE((out.state & 0x4u) != 0u);  // N above
    REQUIRE((out.state & 0x8u) == 0u);  // B below
}

TEST_CASE("splosh: outputs change when inputs change", "[splosh]") {
    lets_splosh_modulator m;
    m.update("c", 0.5f); m.update("t", 0.5f);
    m.update("n", 0.5f); m.update("b", 0.5f);
    const auto out1 = m.tick(0.0, rate);

    m.update("b", 0.9f);
    const auto out2 = m.tick(0.0, rate);
    REQUIRE(out1.outputs[0x7] != out2.outputs[0x7]);
}

TEST_CASE("splosh: unknown update key is a no-op", "[splosh]") {
    lets_splosh_modulator m;
    REQUIRE_NOTHROW(m.update("bogus", 1.0f));
    REQUIRE_NOTHROW(m.tick(0.0, rate));
}
