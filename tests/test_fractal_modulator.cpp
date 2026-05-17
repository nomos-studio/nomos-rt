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
        out.push_back(m.tick(0.0, tick_rate).cv);
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
    const float v = m.tick(0.0, tick_rate).cv;
    REQUIRE(!std::isnan(v));
    REQUIRE(v >= -1.0f);
    REQUIRE(v <= 1.0f);
}

TEST_CASE("fractal_modulator: gate false before threshold crossed", "[fractal]") {
    fractal_modulator m;
    // zero tick_rate: no phase advance, no gate computation — gate is always false
    REQUIRE(!m.tick(0.0, 0.0f).gate);
}

// ---------------------------------------------------------------------------
// Output range
// ---------------------------------------------------------------------------

TEST_CASE("fractal_modulator: output in [-1, 1] across 1000 ticks, all shapes", "[fractal]") {
    for (int shape : {0, 1, 2}) {
        fractal_modulator m;
        m.update("shape", static_cast<float>(shape));
        for (int i = 0; i < 1000; ++i) {
            const float v = m.tick(0.0, tick_rate).cv;
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
        const float v = m.tick(0.0, tick_rate).cv;
        REQUIRE(v >= -0.5f);
        REQUIRE(v <= 0.5f);
    }
}

TEST_CASE("fractal_modulator: depth zero produces zero output", "[fractal]") {
    fractal_modulator m;
    m.update("depth", 0.0f);
    for (int i = 0; i < 50; ++i)
        REQUIRE(m.tick(0.0, tick_rate).cv == Catch::Approx(0.0f).margin(1e-6f));
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
    fractal_modulator m;
    m.update("octaves",   1.0f);
    m.update("base_rate", 1.0f);
    m.update("shape",     0.0f);

    float min_v =  1.0f, max_v = -1.0f;
    for (int i = 0; i < 100; ++i) {
        const float v = m.tick(0.0, tick_rate).cv;
        if (v < min_v) min_v = v;
        if (v > max_v) max_v = v;
    }
    REQUIRE(max_v >= 0.9f);
    REQUIRE(min_v <= -0.9f);
}

TEST_CASE("fractal_modulator: more octaves still in range", "[fractal]") {
    for (int oct : {1, 2, 4, 6, 8}) {
        fractal_modulator m;
        m.update("octaves", static_cast<float>(oct));
        for (int i = 0; i < 200; ++i) {
            const float v = m.tick(0.0, tick_rate).cv;
            REQUIRE(v >= -1.0f);
            REQUIRE(v <= 1.0f);
        }
    }
}

// ---------------------------------------------------------------------------
// Persistence character
// ---------------------------------------------------------------------------

TEST_CASE("fractal_modulator: persistence near-zero dominated by base octave", "[fractal]") {
    fractal_modulator single;
    single.update("octaves",     1.0f);
    single.update("base_rate",   1.0f);
    single.update("shape",       0.0f);

    fractal_modulator multi;
    multi.update("octaves",     8.0f);
    multi.update("persistence", 0.01f);
    multi.update("base_rate",   1.0f);
    multi.update("shape",       0.0f);

    float s_max = -1.0f, m_max = -1.0f;
    for (int i = 0; i < 200; ++i) {
        s_max = std::max(s_max, single.tick(0.0, tick_rate).cv);
        m_max = std::max(m_max, multi.tick(0.0, tick_rate).cv);
    }
    REQUIRE(s_max >= 0.8f);
    REQUIRE(m_max >= 0.8f);
}

TEST_CASE("fractal_modulator: high persistence still in range", "[fractal]") {
    fractal_modulator m;
    m.update("persistence", 2.0f);
    m.update("octaves", 8.0f);
    for (int i = 0; i < 500; ++i) {
        const float v = m.tick(0.0, tick_rate).cv;
        REQUIRE(v >= -1.0f);
        REQUIRE(v <= 1.0f);
    }
}

// ---------------------------------------------------------------------------
// Gate output
// ---------------------------------------------------------------------------

TEST_CASE("fractal_modulator: gate fires at threshold crossing", "[fractal]") {
    fractal_modulator m;
    m.update("base_rate", 1.0f);
    m.update("threshold", 0.0f);

    int gate_count = 0;
    for (int i = 0; i < 500; ++i) {
        if (m.tick(0.0, tick_rate).gate) ++gate_count;
    }
    REQUIRE(gate_count > 0);
}

TEST_CASE("fractal_modulator: gate fires multiple times across cycles", "[fractal]") {
    fractal_modulator m;
    m.update("base_rate", 1.0f);
    m.update("threshold", 0.0f);

    int gate_count = 0;
    for (int i = 0; i < 1000; ++i) {
        if (m.tick(0.0, tick_rate).gate) ++gate_count;
    }
    REQUIRE(gate_count > 1);
}

TEST_CASE("fractal_modulator: gate fires once per rising edge, not continuously", "[fractal]") {
    fractal_modulator m;
    m.update("base_rate", 1.0f);
    m.update("threshold", 0.0f);

    bool last_gate = false;
    for (int i = 0; i < 500; ++i) {
        const auto out = m.tick(0.0, tick_rate);
        if (out.gate) {
            REQUIRE(!last_gate);
        }
        last_gate = out.gate;
    }
}

TEST_CASE("fractal_modulator: gate does not fire with threshold=1 (unreachable)", "[fractal]") {
    fractal_modulator m;
    m.update("base_rate", 1.0f);
    m.update("threshold", 1.0f);

    for (int i = 0; i < 500; ++i) {
        REQUIRE(!m.tick(0.0, tick_rate).gate);
    }
}

// ---------------------------------------------------------------------------
// Parameter validation
// ---------------------------------------------------------------------------

TEST_CASE("fractal_modulator: unknown key is a no-op", "[fractal]") {
    fractal_modulator m;
    REQUIRE_NOTHROW(m.update("nonexistent", 0.5f));
    REQUIRE(!std::isnan(m.tick(0.0, tick_rate).cv));
}

TEST_CASE("fractal_modulator: extreme parameter values do not crash", "[fractal]") {
    fractal_modulator m;
    REQUIRE_NOTHROW(m.update("base_rate",   1e9f));
    REQUIRE_NOTHROW(m.update("lacunarity",  -5.0f));
    REQUIRE_NOTHROW(m.update("persistence", 999.0f));
    REQUIRE(!std::isnan(m.tick(0.0, tick_rate).cv));
}

TEST_CASE("fractal_modulator: zero tick_rate_hz returns current output unchanged", "[fractal]") {
    fractal_modulator m;
    m.update("base_rate", 1.0f);
    const float v0 = m.tick(0.0, tick_rate).cv;
    const float v1 = m.tick(0.0, 0.0f).cv;
    REQUIRE(v1 == Catch::Approx(v0));
}
