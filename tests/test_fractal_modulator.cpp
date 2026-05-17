// SPDX-License-Identifier: LGPL-2.1-or-later
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "fractal_modulator.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

using nomos::rt::fractal_modulator;

namespace {

constexpr float tick_rate = 100.0f;

std::vector<float> collect(fractal_modulator& m, int n) {
    std::vector<float> out;
    out.reserve(n);
    for (int i = 0; i < n; ++i)
        out.push_back(m.tick(0.0, tick_rate));
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TEST_CASE("fractal_modulator: default construction does not crash", "[fractal]") {
    REQUIRE_NOTHROW(fractal_modulator{});
}

TEST_CASE("fractal_modulator: initial output is finite and in [-1, 1]", "[fractal]") {
    fractal_modulator m;
    const float v = m.tick(0.0, tick_rate);
    REQUIRE(!std::isnan(v));
    REQUIRE(v >= -1.0f);
    REQUIRE(v <= 1.0f);
}

TEST_CASE("fractal_modulator: gate and output false/zero before first tick", "[fractal]") {
    fractal_modulator m;
    REQUIRE(!m.gate_out());
}

// ---------------------------------------------------------------------------
// Output range
// ---------------------------------------------------------------------------

TEST_CASE("fractal_modulator: output in [-1, 1] across 1000 ticks, all shapes", "[fractal]") {
    for (int shape : {0, 1, 2}) {
        fractal_modulator m;
        m.update("shape", static_cast<float>(shape));
        for (int i = 0; i < 1000; ++i) {
            const float v = m.tick(0.0, tick_rate);
            REQUIRE(v >= -1.0f);
            REQUIRE(v <= 1.0f);
            REQUIRE(!std::isnan(v));
        }
    }
}

TEST_CASE("fractal_modulator: depth scales output", "[fractal]") {
    fractal_modulator m;
    m.update("depth", 0.5f);
    m.update("base_rate", 1.0f);
    for (int i = 0; i < 200; ++i) {
        const float v = m.tick(0.0, tick_rate);
        REQUIRE(v >= -0.5f);
        REQUIRE(v <= 0.5f);
    }
}

TEST_CASE("fractal_modulator: depth zero produces zero output", "[fractal]") {
    fractal_modulator m;
    m.update("depth", 0.0f);
    for (int i = 0; i < 50; ++i)
        REQUIRE(m.tick(0.0, tick_rate) == Catch::Approx(0.0f).margin(1e-6f));
}

// ---------------------------------------------------------------------------
// Output variation
// ---------------------------------------------------------------------------

TEST_CASE("fractal_modulator: smooth shape output varies", "[fractal]") {
    fractal_modulator m;
    m.update("shape", 0.0f);
    m.update("base_rate", 1.0f);
    const auto vals = collect(m, 64);
    const float first = vals.front();
    REQUIRE(std::any_of(vals.begin(), vals.end(), [first](float v){ return v != first; }));
}

TEST_CASE("fractal_modulator: angular shape output varies", "[fractal]") {
    fractal_modulator m;
    m.update("shape", 1.0f);
    m.update("base_rate", 1.0f);
    const auto vals = collect(m, 64);
    const float first = vals.front();
    REQUIRE(std::any_of(vals.begin(), vals.end(), [first](float v){ return v != first; }));
}

TEST_CASE("fractal_modulator: stepped shape output varies", "[fractal]") {
    fractal_modulator m;
    m.update("shape", 2.0f);
    m.update("base_rate", 1.0f);
    const auto vals = collect(m, 200);
    std::set<float> seen(vals.begin(), vals.end());
    REQUIRE(seen.size() > 1u);
}

// ---------------------------------------------------------------------------
// Octave count
// ---------------------------------------------------------------------------

TEST_CASE("fractal_modulator: octaves=1 produces a single-oscillator output", "[fractal]") {
    // A single sine at 1 Hz / 100 Hz tick_rate should complete one cycle in
    // exactly 100 ticks.  With one octave, the output is simply sin(2π×phase).
    // Verify the output spans the expected range over 100+ ticks.
    fractal_modulator m;
    m.update("octaves",   1.0f);
    m.update("base_rate", 1.0f);
    m.update("shape",     0.0f);

    float min_v =  1.0f, max_v = -1.0f;
    for (int i = 0; i < 100; ++i) {
        const float v = m.tick(0.0, tick_rate);
        if (v < min_v) min_v = v;
        if (v > max_v) max_v = v;
    }
    // One sine cycle: should reach close to ±1.
    REQUIRE(max_v >= 0.9f);
    REQUIRE(min_v <= -0.9f);
}

TEST_CASE("fractal_modulator: more octaves still in range", "[fractal]") {
    for (int oct : {1, 2, 4, 6, 8}) {
        fractal_modulator m;
        m.update("octaves", static_cast<float>(oct));
        for (int i = 0; i < 200; ++i) {
            const float v = m.tick(0.0, tick_rate);
            REQUIRE(v >= -1.0f);
            REQUIRE(v <= 1.0f);
        }
    }
}

// ---------------------------------------------------------------------------
// Persistence character
// ---------------------------------------------------------------------------

TEST_CASE("fractal_modulator: persistence near-zero dominated by base octave", "[fractal]") {
    // persistence=0.01 → all higher octaves contribute < 1% each.
    // Result should behave like a near-pure single oscillator.
    fractal_modulator single;
    single.update("octaves",     1.0f);
    single.update("base_rate",   1.0f);
    single.update("shape",       0.0f);

    fractal_modulator multi;
    multi.update("octaves",     8.0f);
    multi.update("persistence", 0.01f);
    multi.update("base_rate",   1.0f);
    multi.update("shape",       0.0f);

    // Both should produce broadly similar peak excursions.
    float s_max = -1.0f, m_max = -1.0f;
    for (int i = 0; i < 200; ++i) {
        s_max = std::max(s_max, single.tick(0.0, tick_rate));
        m_max = std::max(m_max, multi.tick(0.0, tick_rate));
    }
    // Both should reach above 0.8 (dominated by base sine).
    REQUIRE(s_max >= 0.8f);
    REQUIRE(m_max >= 0.8f);
}

TEST_CASE("fractal_modulator: high persistence still in range", "[fractal]") {
    fractal_modulator m;
    m.update("persistence", 2.0f);  // upper octaves dominate
    m.update("octaves", 8.0f);
    for (int i = 0; i < 500; ++i) {
        const float v = m.tick(0.0, tick_rate);
        REQUIRE(v >= -1.0f);
        REQUIRE(v <= 1.0f);
    }
}

// ---------------------------------------------------------------------------
// Gate output
// ---------------------------------------------------------------------------

TEST_CASE("fractal_modulator: gate fires at threshold crossing", "[fractal]") {
    // threshold=0: fires every time output crosses zero from below.
    // base_rate=1 Hz → ~100 ticks per cycle → several crossings in 500 ticks.
    fractal_modulator m;
    m.update("base_rate", 1.0f);
    m.update("threshold", 0.0f);

    int gate_count = 0;
    for (int i = 0; i < 500; ++i) {
        m.tick(0.0, tick_rate);
        if (m.gate_out()) ++gate_count;
    }
    REQUIRE(gate_count > 0);
}

TEST_CASE("fractal_modulator: gate fires multiple times across cycles", "[fractal]") {
    fractal_modulator m;
    m.update("base_rate", 1.0f);
    m.update("threshold", 0.0f);

    int gate_count = 0;
    for (int i = 0; i < 1000; ++i) {
        m.tick(0.0, tick_rate);
        if (m.gate_out()) ++gate_count;
    }
    REQUIRE(gate_count > 1);
}

TEST_CASE("fractal_modulator: gate fires once per rising edge, not continuously", "[fractal]") {
    // After a gate fires, the next consecutive tick should not fire again
    // unless the output has dipped below the threshold and crossed back up.
    fractal_modulator m;
    m.update("base_rate", 1.0f);
    m.update("threshold", 0.0f);

    bool last_gate = false;
    for (int i = 0; i < 500; ++i) {
        m.tick(0.0, tick_rate);
        // Consecutive gate ticks would mean it fired two ticks in a row —
        // only valid if output dipped below threshold between them, which
        // is impossible in two consecutive ticks with a smooth sinusoid.
        if (m.gate_out()) {
            REQUIRE(!last_gate);
        }
        last_gate = m.gate_out();
    }
}

TEST_CASE("fractal_modulator: gate does not fire with threshold=1 (unreachable)", "[fractal]") {
    fractal_modulator m;
    m.update("base_rate", 1.0f);
    m.update("threshold", 1.0f);  // output never exceeds 1.0 exactly

    for (int i = 0; i < 500; ++i) {
        m.tick(0.0, tick_rate);
        REQUIRE(!m.gate_out());
    }
}

// ---------------------------------------------------------------------------
// Parameter validation
// ---------------------------------------------------------------------------

TEST_CASE("fractal_modulator: unknown key is a no-op", "[fractal]") {
    fractal_modulator m;
    REQUIRE_NOTHROW(m.update("nonexistent", 0.5f));
    REQUIRE(!std::isnan(m.tick(0.0, tick_rate)));
}

TEST_CASE("fractal_modulator: extreme parameter values do not crash", "[fractal]") {
    fractal_modulator m;
    REQUIRE_NOTHROW(m.update("base_rate",   1e9f));
    REQUIRE_NOTHROW(m.update("lacunarity",  -5.0f));
    REQUIRE_NOTHROW(m.update("persistence", 999.0f));
    REQUIRE(!std::isnan(m.tick(0.0, tick_rate)));
}

TEST_CASE("fractal_modulator: zero tick_rate_hz returns current output unchanged", "[fractal]") {
    fractal_modulator m;
    m.update("base_rate", 1.0f);
    const float v0 = m.tick(0.0, tick_rate);
    const float v1 = m.tick(0.0, 0.0f);
    REQUIRE(v1 == Catch::Approx(v0));
}
