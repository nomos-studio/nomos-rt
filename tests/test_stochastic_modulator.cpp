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
        out.push_back(m.tick(0.0, tick_rate).cv);
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
    const float v = m.tick(0.0, tick_rate).cv;
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
        const float v = m.tick(0.0, tick_rate).cv;
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
        const float v = m.tick(0.0, tick_rate).cv;
        REQUIRE(v >= -0.5f);
        REQUIRE(v <= 0.5f);
    }
}

TEST_CASE("stochastic_modulator: depth zero produces zero output", "[stochastic]") {
    stochastic_modulator m;
    m.update("depth", 0.0f);
    for (int i = 0; i < 50; ++i)
        REQUIRE(m.tick(0.0, tick_rate).cv == Catch::Approx(0.0f).margin(1e-6f));
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
    m.update("bias",   0.75f);

    for (int i = 0; i < 100; ++i) m.tick(0.0, tick_rate);

    std::set<float> seen;
    for (int i = 0; i < 200; ++i)
        seen.insert(m.tick(0.0, tick_rate).cv);

    REQUIRE(seen.size() == 1u);
}

TEST_CASE("stochastic_modulator: steps quantises output", "[stochastic]") {
    stochastic_modulator m;
    m.update("rate",  5.0f);
    m.update("steps", 4.0f);

    std::set<float> seen;
    for (int i = 0; i < 500; ++i)
        seen.insert(m.tick(0.0, tick_rate).cv);

    for (float v : seen) {
        const float scaled = (v + 1.0f) * 0.5f;
        const float rem = std::fmod(std::abs(scaled * 4.0f), 1.0f);
        REQUIRE((rem < 1e-4f || rem > 1.0f - 1e-4f));
    }
}

// ---------------------------------------------------------------------------
// Gate output
// ---------------------------------------------------------------------------

TEST_CASE("stochastic_modulator: gate fires at approximately the clock rate", "[stochastic]") {
    stochastic_modulator m;
    m.update("rate", 5.0f);
    m.update("bias", 1.0f);

    int gate_count = 0;
    for (int i = 0; i < 500; ++i) {
        if (m.tick(0.0, tick_rate).gate) ++gate_count;
    }
    REQUIRE(gate_count >= 15);
    REQUIRE(gate_count <= 35);
}

TEST_CASE("stochastic_modulator: bias=0 produces no gates", "[stochastic]") {
    stochastic_modulator m;
    m.update("rate", 5.0f);
    m.update("bias", 0.0f);

    for (int i = 0; i < 500; ++i) {
        REQUIRE(!m.tick(0.0, tick_rate).gate);
    }
}

TEST_CASE("stochastic_modulator: gate fires at most once per clock period", "[stochastic]") {
    stochastic_modulator m;
    m.update("rate", 5.0f);
    m.update("bias", 1.0f);

    bool prev_gate = false;
    for (int i = 0; i < 500; ++i) {
        const auto out = m.tick(0.0, tick_rate);
        if (out.gate)
            REQUIRE(!prev_gate);
        prev_gate = out.gate;
    }
}

// ---------------------------------------------------------------------------
// Déjà vu
// ---------------------------------------------------------------------------

TEST_CASE("stochastic_modulator: deja_vu=0.5 locks to a bounded CV vocabulary", "[stochastic]") {
    stochastic_modulator m;
    m.update("rate",    5.0f);
    m.update("length",  4.0f);
    m.update("spread",  1.0f);
    m.update("deja_vu", 0.5f);

    for (int i = 0; i < 400; ++i) m.tick(0.0, tick_rate);

    std::set<float> seen;
    for (int i = 0; i < 1000; ++i)
        seen.insert(m.tick(0.0, tick_rate).cv);

    REQUIRE(seen.size() <= 4u);
    REQUIRE(seen.size() >= 2u);
}

TEST_CASE("stochastic_modulator: deja_vu=0 produces variation over many events", "[stochastic]") {
    stochastic_modulator m;
    m.update("rate",    10.0f);
    m.update("bias",    1.0f);
    m.update("deja_vu", 0.0f);
    m.update("spread",  1.0f);

    std::set<float> seen;
    for (int i = 0; i < 500; ++i) {
        const auto out = m.tick(0.0, tick_rate);
        if (out.gate) seen.insert(out.cv);
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
        ++since_last;
        if (m.tick(0.0, tick_rate).gate) {
            intervals.push_back(since_last);
            since_last = 0;
        }
    }
    if (intervals.size() > 4) {
        int min_i = intervals[0], max_i = intervals[0];
        for (int v : intervals) { min_i = std::min(min_i, v); max_i = std::max(max_i, v); }
        REQUIRE(max_i > min_i);
    }
}

// ---------------------------------------------------------------------------
// Parameter validation
// ---------------------------------------------------------------------------

TEST_CASE("stochastic_modulator: unknown key is a no-op", "[stochastic]") {
    stochastic_modulator m;
    REQUIRE_NOTHROW(m.update("nonexistent", 0.5f));
    REQUIRE(!std::isnan(m.tick(0.0, tick_rate).cv));
}

TEST_CASE("stochastic_modulator: extreme parameter values do not crash", "[stochastic]") {
    stochastic_modulator m;
    REQUIRE_NOTHROW(m.update("rate",    -1.0f));
    REQUIRE_NOTHROW(m.update("rate",    1e9f));
    REQUIRE_NOTHROW(m.update("jitter", -1.0f));
    REQUIRE_NOTHROW(m.update("jitter",  2.0f));
    REQUIRE(!std::isnan(m.tick(0.0, tick_rate).cv));
}

TEST_CASE("stochastic_modulator: zero tick_rate_hz returns current output", "[stochastic]") {
    stochastic_modulator m;
    m.update("rate", 5.0f);
    const float v0 = m.tick(0.0, tick_rate).cv;
    const float v1 = m.tick(0.0, 0.0f).cv;
    REQUIRE(v1 == Catch::Approx(v0));
}
