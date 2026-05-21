// SPDX-License-Identifier: LGPL-2.1-or-later
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "cv_channel_decoder.hpp"

#include <cmath>

using nomos::rt::cv_channel_decoder;

namespace {
constexpr float tick_rate = 375.0f;
} // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TEST_CASE("cv_channel_decoder: default construction does not crash", "[cv-channel-decoder]") {
    REQUIRE_NOTHROW(cv_channel_decoder{});
}

TEST_CASE("cv_channel_decoder: channels clamped to [1, 8]", "[cv-channel-decoder]") {
    cv_channel_decoder lo{-5};
    cv_channel_decoder hi{20};
    // Both should tick without crash; actual channel count is internal.
    REQUIRE_NOTHROW(lo.tick(0.0, tick_rate));
    REQUIRE_NOTHROW(hi.tick(0.0, tick_rate));
}

// ---------------------------------------------------------------------------
// N=1 Schmitt trigger behaviour
// ---------------------------------------------------------------------------

TEST_CASE("cv_channel_decoder N=1: gate fires when span is within detection window",
          "[cv-channel-decoder]") {
    cv_channel_decoder m{1};
    // N=1, centre=0, space=1.0 → active window = [-1, 1] (whole range).
    m.update("span", 0.0f);
    auto out = m.tick(0.0, tick_rate);
    REQUIRE(out.gate);
    REQUIRE(out.state == 0x1u);
}

TEST_CASE("cv_channel_decoder N=1 space=0.25: gate fires near centre, quiet at edges",
          "[cv-channel-decoder]") {
    cv_channel_decoder m{1};
    m.update("space", 0.25f);  // active window = [-0.25, 0.25]

    m.update("span", 0.0f);
    REQUIRE(m.tick(0.0, tick_rate).gate);

    m.update("span", 0.3f);
    REQUIRE_FALSE(m.tick(0.0, tick_rate).gate);

    m.update("span", -0.3f);
    REQUIRE_FALSE(m.tick(0.0, tick_rate).gate);
}

// ---------------------------------------------------------------------------
// Multi-channel band detection
// ---------------------------------------------------------------------------

TEST_CASE("cv_channel_decoder N=4: correct channel fires per band", "[cv-channel-decoder]") {
    cv_channel_decoder m{4};
    // 4 bands in [-1,1], each 0.5 wide:
    //   ch0: [-1.0, -0.5]  ctr=-0.75
    //   ch1: [-0.5,  0.0]  ctr=-0.25
    //   ch2: [ 0.0,  0.5]  ctr=+0.25
    //   ch3: [ 0.5,  1.0]  ctr=+0.75

    auto check = [&](float span, uint32_t expected_bit) {
        m.update("span", span);
        const auto out = m.tick(0.0, tick_rate);
        REQUIRE(out.state == expected_bit);
        REQUIRE(out.gate == (expected_bit != 0));
    };

    check(-0.75f, 0x1u);  // ch0
    check(-0.25f, 0x2u);  // ch1
    check( 0.25f, 0x4u);  // ch2
    check( 0.75f, 0x8u);  // ch3
}

TEST_CASE("cv_channel_decoder N=4 space>1: adjacent channels overlap", "[cv-channel-decoder]") {
    cv_channel_decoder m{4};
    m.update("space", 1.5f);  // bands overlap by 0.5× their half-width

    // At a band boundary both neighbouring channels should be active.
    m.update("span", -0.5f);  // boundary between ch0 and ch1
    const auto out = m.tick(0.0, tick_rate);
    REQUIRE((out.state & 0x1u) != 0);  // ch0 active
    REQUIRE((out.state & 0x2u) != 0);  // ch1 active
}

TEST_CASE("cv_channel_decoder N=8: all channels addressable", "[cv-channel-decoder]") {
    cv_channel_decoder m{8};
    // band width = 0.25, centres at -0.875, -0.625, ..., +0.875
    for (int ch = 0; ch < 8; ++ch) {
        const float ctr = -1.0f + (2.0f * static_cast<float>(ch) + 1.0f) / 8.0f;
        m.update("span", ctr);
        const auto out = m.tick(0.0, tick_rate);
        REQUIRE((out.state & (1u << ch)) != 0);
    }
}

// ---------------------------------------------------------------------------
// Auxiliary outputs
// ---------------------------------------------------------------------------

TEST_CASE("cv_channel_decoder: aux passthrough equals span", "[cv-channel-decoder]") {
    cv_channel_decoder m{1};
    m.update("span", 0.42f);
    const auto out = m.tick(0.0, tick_rate);
    REQUIRE(out.aux == Catch::Approx(0.42f));
}

TEST_CASE("cv_channel_decoder: velocity is 0 on first tick", "[cv-channel-decoder]") {
    cv_channel_decoder m{1};
    m.update("span", 0.5f);
    const auto out = m.tick(0.0, tick_rate);
    REQUIRE(out.cv == Catch::Approx(0.0f));
}

TEST_CASE("cv_channel_decoder: velocity proportional to rate of span change",
          "[cv-channel-decoder]") {
    cv_channel_decoder m{1};
    m.update("span", -1.0f);
    m.tick(0.0, tick_rate);  // first tick — seeds prev_span

    // Full-range sweep [-1,1] in one second: delta=2, rate=tick_rate
    // velocity = min(1, |2| * tick_rate * 0.5 / tick_rate) = min(1, 1) = 1.0
    // At one tick step: delta_per_tick = 2.0 / tick_rate
    const float delta = 2.0f / tick_rate;
    m.update("span", -1.0f + delta);
    const auto out = m.tick(0.0, tick_rate);
    REQUIRE(out.cv == Catch::Approx(1.0f).margin(1e-4f));
}

TEST_CASE("cv_channel_decoder: velocity is 0 for stationary span", "[cv-channel-decoder]") {
    cv_channel_decoder m{1};
    m.update("span", 0.5f);
    m.tick(0.0, tick_rate);  // seed

    for (int i = 0; i < 20; ++i) {
        const auto out = m.tick(0.0, tick_rate);
        REQUIRE(out.cv == Catch::Approx(0.0f).margin(1e-6f));
    }
}

// ---------------------------------------------------------------------------
// Clock-latch mode
// ---------------------------------------------------------------------------

TEST_CASE("cv_channel_decoder: clocked=off passes state immediately", "[cv-channel-decoder]") {
    cv_channel_decoder m{4};
    m.update("span", -0.75f);  // ch0 active
    const auto out = m.tick(0.0, tick_rate);
    REQUIRE(out.state == 0x1u);
}

TEST_CASE("cv_channel_decoder: clocked=on holds state until clock_tick", "[cv-channel-decoder]") {
    cv_channel_decoder m{4};
    m.update("clocked", 1.0f);

    // Before any clock_tick, latched_state is 0.
    m.update("span", -0.75f);  // ch0 would be active
    auto out = m.tick(0.0, tick_rate);
    REQUIRE(out.state == 0x0u);  // not yet latched

    // Fire a clock edge — now the current input latches.
    m.update("clock_tick", 1.0f);
    out = m.tick(0.0, tick_rate);
    REQUIRE(out.state == 0x1u);  // ch0 latched

    // Move span to ch1 — output should still show ch0 (no new clock).
    m.update("span", -0.25f);
    out = m.tick(0.0, tick_rate);
    REQUIRE(out.state == 0x1u);  // still ch0

    // Another clock edge — now ch1 latches.
    m.update("clock_tick", 1.0f);
    out = m.tick(0.0, tick_rate);
    REQUIRE(out.state == 0x2u);
}

TEST_CASE("cv_channel_decoder: clock_tick is consumed (one-shot)", "[cv-channel-decoder]") {
    cv_channel_decoder m{4};
    m.update("clocked", 1.0f);
    m.update("span", -0.75f);
    m.update("clock_tick", 1.0f);

    m.tick(0.0, tick_rate);  // consumes the clock edge, latches ch0

    // Move span and tick again — no new clock, state should not update.
    m.update("span", -0.25f);
    const auto out = m.tick(0.0, tick_rate);
    REQUIRE(out.state == 0x1u);  // still ch0, not ch1
}

// ---------------------------------------------------------------------------
// Parameter validation
// ---------------------------------------------------------------------------

TEST_CASE("cv_channel_decoder: unknown key is a no-op", "[cv-channel-decoder]") {
    cv_channel_decoder m{1};
    REQUIRE_NOTHROW(m.update("nonexistent", 1.0f));
    REQUIRE_NOTHROW(m.tick(0.0, tick_rate));
}

TEST_CASE("cv_channel_decoder: extreme span values do not crash", "[cv-channel-decoder]") {
    cv_channel_decoder m{4};
    REQUIRE_NOTHROW(m.update("span",  1e9f));
    REQUIRE_NOTHROW(m.update("span", -1e9f));
    REQUIRE_NOTHROW(m.tick(0.0, tick_rate));
}

TEST_CASE("cv_channel_decoder: zero tick_rate_hz does not crash", "[cv-channel-decoder]") {
    cv_channel_decoder m{1};
    m.update("span", 0.3f);
    m.tick(0.0, tick_rate);  // seed
    REQUIRE_NOTHROW(m.tick(0.0, 0.0f));
    const auto out = m.tick(0.0, 0.0f);
    REQUIRE(!std::isnan(out.cv));
}
