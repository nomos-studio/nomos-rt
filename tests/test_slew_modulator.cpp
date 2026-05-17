// SPDX-License-Identifier: LGPL-2.1-or-later
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "slew_modulator.hpp"

#include <cmath>

using nomos::rt::slew_modulator;

namespace {

constexpr float tick_rate = 100.0f;

// Fast rise/fall (10ms at 100Hz) reaches threshold in ~3 ticks.
slew_modulator make_fast() {
    slew_modulator m;
    m.update("rise", 0.01f);
    m.update("fall", 0.01f);
    return m;
}

} // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TEST_CASE("slew_modulator: default construction does not crash", "[slew]") {
    REQUIRE_NOTHROW(slew_modulator{});
}

TEST_CASE("slew_modulator: initial output is zero", "[slew]") {
    slew_modulator m;
    REQUIRE(m.tick(0.0, tick_rate).cv == Catch::Approx(0.0f).margin(1e-5f));
}

TEST_CASE("slew_modulator: eor and eoc false at construction", "[slew]") {
    slew_modulator m;
    const auto out = m.tick(0.0, tick_rate);
    REQUIRE(!out.gate);
    REQUIRE(!out.gate2);
}

// ---------------------------------------------------------------------------
// Output range and depth
// ---------------------------------------------------------------------------

TEST_CASE("slew_modulator: output in [-1, 1] across lag, trig, and cycle modes", "[slew]") {
    auto m = make_fast();
    m.update("cycle", 1.0f);
    for (int i = 0; i < 200; ++i) {
        const float v = m.tick(0.0, tick_rate).cv;
        REQUIRE(v >= -1.0f);
        REQUIRE(v <= 1.0f);
        REQUIRE(!std::isnan(v));
    }
}

TEST_CASE("slew_modulator: depth scales output in cycle mode", "[slew]") {
    auto m = make_fast();
    m.update("depth", 0.5f);
    m.update("cycle", 1.0f);
    for (int i = 0; i < 100; ++i) {
        const float v = m.tick(0.0, tick_rate).cv;
        REQUIRE(v >= -0.5f);
        REQUIRE(v <= 0.5f);
    }
}

TEST_CASE("slew_modulator: depth zero produces zero output", "[slew]") {
    auto m = make_fast();
    m.update("depth", 0.0f);
    m.update("cycle", 1.0f);
    for (int i = 0; i < 20; ++i)
        REQUIRE(m.tick(0.0, tick_rate).cv == Catch::Approx(0.0f).margin(1e-6f));
}

// ---------------------------------------------------------------------------
// Lag mode
// ---------------------------------------------------------------------------

TEST_CASE("slew_modulator: lag mode approaches input", "[slew][lag]") {
    slew_modulator m;
    m.update("rise", 0.1f);
    m.update("input", 1.0f);

    for (int i = 0; i < 500; ++i)
        m.tick(0.0, tick_rate);

    REQUIRE(m.tick(0.0, tick_rate).cv >= 0.99f);
}

TEST_CASE("slew_modulator: lag mode uses rise for upward movement", "[slew][lag]") {
    slew_modulator m;
    m.update("rise", 0.1f);
    m.update("fall", 0.001f);
    m.update("input", 1.0f);

    const float v0 = m.tick(0.0, tick_rate).cv;
    REQUIRE(v0 > 0.0f);
    REQUIRE(v0 < 0.5f);
}

TEST_CASE("slew_modulator: lag mode uses fall for downward movement", "[slew][lag]") {
    slew_modulator m;
    m.update("rise",  0.001f);
    m.update("fall",  0.1f);
    m.update("input", 1.0f);
    for (int i = 0; i < 200; ++i) m.tick(0.0, tick_rate);

    m.update("input", -1.0f);
    const float before = m.tick(0.0, tick_rate).cv;
    const float after  = m.tick(0.0, tick_rate).cv;
    REQUIRE(after < before);
    REQUIRE(after > -0.5f);
}

TEST_CASE("slew_modulator: lag mode eor and eoc remain false", "[slew][lag]") {
    slew_modulator m;
    m.update("input", 1.0f);
    for (int i = 0; i < 200; ++i) {
        const auto out = m.tick(0.0, tick_rate);
        REQUIRE(!out.gate);
        REQUIRE(!out.gate2);
    }
}

// ---------------------------------------------------------------------------
// Trig mode
// ---------------------------------------------------------------------------

TEST_CASE("slew_modulator: trig starts a rise-fall cycle", "[slew][trig]") {
    auto m = make_fast();
    m.update("trig", 1.0f);

    const float v0 = m.tick(0.0, tick_rate).cv;
    REQUIRE(v0 > -1.0f);

    bool saw_eor = false;
    for (int i = 0; i < 20; ++i) {
        if (m.tick(0.0, tick_rate).gate) { saw_eor = true; break; }
    }
    REQUIRE(saw_eor);
}

TEST_CASE("slew_modulator: trig eor fires exactly once", "[slew][trig]") {
    auto m = make_fast();
    m.update("trig", 1.0f);

    int eor_count = 0;
    for (int i = 0; i < 50; ++i) {
        if (m.tick(0.0, tick_rate).gate) ++eor_count;
    }
    REQUIRE(eor_count == 1);
}

TEST_CASE("slew_modulator: trig eoc fires after fall completes", "[slew][trig]") {
    auto m = make_fast();
    m.update("trig", 1.0f);

    bool saw_eoc = false;
    for (int i = 0; i < 50; ++i) {
        if (m.tick(0.0, tick_rate).gate2) { saw_eoc = true; break; }
    }
    REQUIRE(saw_eoc);
}

TEST_CASE("slew_modulator: after trig cycle completes, returns to lag", "[slew][trig]") {
    auto m = make_fast();
    m.update("input", 0.5f);
    m.update("trig", 1.0f);

    bool saw_eoc = false;
    for (int i = 0; i < 50 && !saw_eoc; ++i)
        saw_eoc = m.tick(0.0, tick_rate).gate2;
    REQUIRE(saw_eoc);

    for (int i = 0; i < 500; ++i) m.tick(0.0, tick_rate);
    REQUIRE(m.tick(0.0, tick_rate).cv >= 0.49f);
}

// ---------------------------------------------------------------------------
// Cycle mode
// ---------------------------------------------------------------------------

TEST_CASE("slew_modulator: cycle mode produces oscillating output", "[slew][cycle]") {
    auto m = make_fast();
    m.update("cycle", 1.0f);

    float min_v =  1.0f;
    float max_v = -1.0f;
    for (int i = 0; i < 200; ++i) {
        const float v = m.tick(0.0, tick_rate).cv;
        if (v < min_v) min_v = v;
        if (v > max_v) max_v = v;
    }
    REQUIRE(max_v >= 0.9f);
    REQUIRE(min_v <= -0.9f);
}

TEST_CASE("slew_modulator: cycle mode eor fires on each rise completion", "[slew][cycle]") {
    auto m = make_fast();
    m.update("cycle", 1.0f);

    int eor_count = 0;
    for (int i = 0; i < 200; ++i) {
        if (m.tick(0.0, tick_rate).gate) ++eor_count;
    }
    REQUIRE(eor_count > 1);
}

TEST_CASE("slew_modulator: cycle mode eoc fires on each fall completion", "[slew][cycle]") {
    auto m = make_fast();
    m.update("cycle", 1.0f);

    int eoc_count = 0;
    for (int i = 0; i < 200; ++i) {
        if (m.tick(0.0, tick_rate).gate2) ++eoc_count;
    }
    REQUIRE(eoc_count > 1);
}

TEST_CASE("slew_modulator: cycle mode asymmetric rise and fall", "[slew][cycle]") {
    slew_modulator m;
    m.update("rise",  0.1f);
    m.update("fall",  0.001f);
    m.update("cycle", 1.0f);

    int ticks_rising = 0, ticks_falling = 0;
    bool in_fall = false;
    bool seen_eor = false;

    for (int i = 0; i < 500; ++i) {
        const auto out = m.tick(0.0, tick_rate);
        if (out.gate)  { in_fall = true;  seen_eor = true; }
        if (out.gate2) { in_fall = false; }
        if (seen_eor) {
            if (in_fall) ++ticks_falling;
            else         ++ticks_rising;
        }
    }
    REQUIRE(ticks_rising > ticks_falling * 5);
}

TEST_CASE("slew_modulator: trig in cycle mode resets cycle", "[slew][cycle]") {
    slew_modulator m;
    m.update("rise",  0.1f);
    m.update("fall",  0.1f);
    m.update("cycle", 1.0f);

    m.tick(0.0, tick_rate);

    m.update("trig", 1.0f);
    const float v = m.tick(0.0, tick_rate).cv;
    REQUIRE(v > -1.0f);
    REQUIRE(v < 0.0f);
}

TEST_CASE("slew_modulator: disabling cycle after trig completes fall, then idles", "[slew][cycle]") {
    auto m = make_fast();
    m.update("cycle", 1.0f);
    m.update("trig", 1.0f);

    bool saw_eoc = false;
    for (int i = 0; i < 50 && !saw_eoc; ++i)
        saw_eoc = m.tick(0.0, tick_rate).gate2;
    REQUIRE(saw_eoc);

    m.update("cycle", 0.0f);

    int extra_eoc = 0;
    for (int i = 0; i < 50; ++i) {
        if (m.tick(0.0, tick_rate).gate2) ++extra_eoc;
    }
    REQUIRE(extra_eoc == 0);
}

// ---------------------------------------------------------------------------
// Parameter validation
// ---------------------------------------------------------------------------

TEST_CASE("slew_modulator: unknown key is a no-op", "[slew]") {
    slew_modulator m;
    REQUIRE_NOTHROW(m.update("nonexistent", 0.5f));
    REQUIRE(!std::isnan(m.tick(0.0, tick_rate).cv));
}

TEST_CASE("slew_modulator: extreme rise/fall times do not crash", "[slew]") {
    slew_modulator m;
    REQUIRE_NOTHROW(m.update("rise", -100.0f));
    REQUIRE_NOTHROW(m.update("fall",  1e9f));
    REQUIRE(!std::isnan(m.tick(0.0, tick_rate).cv));
}

TEST_CASE("slew_modulator: zero tick_rate_hz returns current output", "[slew]") {
    slew_modulator m;
    m.update("input", 1.0f);
    const float v0 = m.tick(0.0, tick_rate).cv;
    const float v1 = m.tick(0.0, 0.0f).cv;
    REQUIRE(v1 == Catch::Approx(v0));
}
