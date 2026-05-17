// SPDX-License-Identifier: LGPL-2.1-or-later
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "stochastic_modulator.hpp"

#include <cmath>
#include <set>
#include <vector>

using nomos::rt::stochastic_modulator;

namespace {

constexpr float tick_rate = 100.0f;

std::vector<float> collect(stochastic_modulator& m, int n) {
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

TEST_CASE("stochastic_modulator: default construction does not crash", "[stochastic]") {
    REQUIRE_NOTHROW(stochastic_modulator{});
}

TEST_CASE("stochastic_modulator: initial output is finite and in [-1, 1]", "[stochastic]") {
    stochastic_modulator m;
    const float v = m.tick(0.0, tick_rate);
    REQUIRE(!std::isnan(v));
    REQUIRE(v >= -1.0f);
    REQUIRE(v <= 1.0f);
}

// ---------------------------------------------------------------------------
// Output range
// ---------------------------------------------------------------------------

TEST_CASE("stochastic_modulator: output in [-1, 1] across many ticks", "[stochastic]") {
    stochastic_modulator m;
    m.update("rate", 5.0f);
    for (int i = 0; i < 500; ++i) {
        const float v = m.tick(0.0, tick_rate);
        REQUIRE(v >= -1.0f);
        REQUIRE(v <= 1.0f);
        REQUIRE(!std::isnan(v));
    }
}

TEST_CASE("stochastic_modulator: depth scales output", "[stochastic]") {
    stochastic_modulator m;
    m.update("rate",  5.0f);
    m.update("depth", 0.5f);
    for (int i = 0; i < 500; ++i) {
        const float v = m.tick(0.0, tick_rate);
        REQUIRE(v >= -0.5f);
        REQUIRE(v <= 0.5f);
    }
}

TEST_CASE("stochastic_modulator: depth zero produces zero output", "[stochastic]") {
    stochastic_modulator m;
    m.update("depth", 0.0f);
    for (int i = 0; i < 50; ++i)
        REQUIRE(m.tick(0.0, tick_rate) == Catch::Approx(0.0f).margin(1e-6f));
}

// ---------------------------------------------------------------------------
// CV variation
// ---------------------------------------------------------------------------

TEST_CASE("stochastic_modulator: output varies with default settings", "[stochastic]") {
    stochastic_modulator m;
    m.update("rate", 5.0f);
    const auto vals = collect(m, 500);
    std::set<float> seen(vals.begin(), vals.end());
    REQUIRE(seen.size() > 1u);
}

TEST_CASE("stochastic_modulator: spread=0 produces constant CV near bias", "[stochastic]") {
    stochastic_modulator m;
    m.update("rate",   5.0f);
    m.update("spread", 0.0f);
    m.update("bias",   0.75f);  // maps to +0.5 in [-1,1]

    // Warm up: let several clock edges fire so the initial cv_out_ is overwritten.
    for (int i = 0; i < 100; ++i) m.tick(0.0, tick_rate);

    // Now all CV values should be the single bias-derived value.
    std::set<float> seen;
    for (int i = 0; i < 200; ++i)
        seen.insert(m.tick(0.0, tick_rate));

    REQUIRE(seen.size() == 1u);
}

TEST_CASE("stochastic_modulator: steps quantises output", "[stochastic]") {
    stochastic_modulator m;
    m.update("rate",  5.0f);
    m.update("steps", 4.0f);  // 5 levels: 0/4, 1/4, 2/4, 3/4, 4/4

    std::set<float> seen;
    for (int i = 0; i < 500; ++i)
        seen.insert(m.tick(0.0, tick_rate));

    // Each value should be one of the 5 quantised levels mapped to [-1,1].
    for (float v : seen) {
        // Un-scale depth=1: v = (k/4)*2 - 1 for k in {0,1,2,3,4}
        const float scaled = (v + 1.0f) * 0.5f;  // back to [0,1]
        const float rem = std::fmod(std::abs(scaled * 4.0f), 1.0f);
        REQUIRE((rem < 1e-4f || rem > 1.0f - 1e-4f));
    }
}

// ---------------------------------------------------------------------------
// Gate output
// ---------------------------------------------------------------------------

TEST_CASE("stochastic_modulator: gate fires at approximately the clock rate", "[stochastic]") {
    // At rate=5Hz, tick_rate=100Hz: ~1 edge per 20 ticks.
    // With bias=1 (always fire), 500 ticks → ~25 gates.
    stochastic_modulator m;
    m.update("rate", 5.0f);
    m.update("bias", 1.0f);

    int gate_count = 0;
    for (int i = 0; i < 500; ++i) {
        m.tick(0.0, tick_rate);
        if (m.gate_out()) ++gate_count;
    }
    // Expect roughly 25; allow 15–35 to be loose enough for jitter=0.
    REQUIRE(gate_count >= 15);
    REQUIRE(gate_count <= 35);
}

TEST_CASE("stochastic_modulator: bias=0 produces no gates", "[stochastic]") {
    stochastic_modulator m;
    m.update("rate", 5.0f);
    m.update("bias", 0.0f);

    for (int i = 0; i < 500; ++i) {
        m.tick(0.0, tick_rate);
        REQUIRE(!m.gate_out());
    }
}

TEST_CASE("stochastic_modulator: gate fires at most once per clock period", "[stochastic]") {
    // Gate should only fire on the tick of a clock edge, not on consecutive ticks.
    stochastic_modulator m;
    m.update("rate", 5.0f);
    m.update("bias", 1.0f);

    bool prev_gate = false;
    for (int i = 0; i < 500; ++i) {
        m.tick(0.0, tick_rate);
        if (m.gate_out())
            REQUIRE(!prev_gate);
        prev_gate = m.gate_out();
    }
}

// ---------------------------------------------------------------------------
// Déjà vu
// ---------------------------------------------------------------------------

TEST_CASE("stochastic_modulator: deja_vu=0.5 locks to a bounded CV vocabulary", "[stochastic]") {
    // With deja_vu=0.5 the loop always replays from its buffer without generating
    // new values.  The CV output must cycle through at most `length` distinct values.
    stochastic_modulator m;
    m.update("rate",    5.0f);
    m.update("length",  4.0f);
    m.update("spread",  1.0f);   // max spread so constructor-seeded values are varied
    m.update("deja_vu", 0.5f);

    // Warm up: let the locked loop spin for many cycles.
    for (int i = 0; i < 400; ++i) m.tick(0.0, tick_rate);

    // Vocabulary should be exactly the 4 buffer positions — no new values added.
    std::set<float> seen;
    for (int i = 0; i < 1000; ++i)
        seen.insert(m.tick(0.0, tick_rate));

    REQUIRE(seen.size() <= 4u);
    REQUIRE(seen.size() >= 2u);  // loop contains distinct values (seeded randomly)
}

TEST_CASE("stochastic_modulator: deja_vu=0 produces variation over many events", "[stochastic]") {
    stochastic_modulator m;
    m.update("rate",    10.0f);
    m.update("bias",    1.0f);
    m.update("deja_vu", 0.0f);
    m.update("spread",  1.0f);

    std::set<float> seen;
    for (int i = 0; i < 500; ++i) {
        m.tick(0.0, tick_rate);
        if (m.gate_out()) seen.insert(m.tick(0.0, tick_rate));
    }
    REQUIRE(seen.size() > 2u);
}

// ---------------------------------------------------------------------------
// Jitter
// ---------------------------------------------------------------------------

TEST_CASE("stochastic_modulator: jitter=1 produces varying inter-event intervals", "[stochastic]") {
    stochastic_modulator m;
    m.update("rate",   2.0f);
    m.update("bias",   1.0f);
    m.update("jitter", 1.0f);

    std::vector<int> intervals;
    int since_last = 0;
    for (int i = 0; i < 2000; ++i) {
        m.tick(0.0, tick_rate);
        ++since_last;
        if (m.gate_out()) {
            intervals.push_back(since_last);
            since_last = 0;
        }
    }
    if (intervals.size() > 4) {
        int min_i = intervals[0], max_i = intervals[0];
        for (int v : intervals) { min_i = std::min(min_i, v); max_i = std::max(max_i, v); }
        REQUIRE(max_i > min_i);  // intervals vary with jitter=1
    }
}

// ---------------------------------------------------------------------------
// Parameter validation
// ---------------------------------------------------------------------------

TEST_CASE("stochastic_modulator: unknown key is a no-op", "[stochastic]") {
    stochastic_modulator m;
    REQUIRE_NOTHROW(m.update("nonexistent", 0.5f));
    REQUIRE(!std::isnan(m.tick(0.0, tick_rate)));
}

TEST_CASE("stochastic_modulator: extreme parameter values do not crash", "[stochastic]") {
    stochastic_modulator m;
    REQUIRE_NOTHROW(m.update("rate",    -1.0f));
    REQUIRE_NOTHROW(m.update("rate",    1e9f));
    REQUIRE_NOTHROW(m.update("jitter", -1.0f));
    REQUIRE_NOTHROW(m.update("jitter",  2.0f));
    REQUIRE(!std::isnan(m.tick(0.0, tick_rate)));
}

TEST_CASE("stochastic_modulator: zero tick_rate_hz returns current output", "[stochastic]") {
    stochastic_modulator m;
    m.update("rate", 5.0f);
    const float v0 = m.tick(0.0, tick_rate);
    const float v1 = m.tick(0.0, 0.0f);
    REQUIRE(v1 == Catch::Approx(v0));
}
