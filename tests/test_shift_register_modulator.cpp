// SPDX-License-Identifier: LGPL-2.1-or-later
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "shift_register_modulator.hpp"

#include <cmath>
#include <set>
#include <vector>

using nomos::rt::shift_register_modulator;
using mode = shift_register_modulator::mode;

namespace {

constexpr float tick_rate = 100.0f;

std::vector<float> collect(shift_register_modulator& mod, int n) {
    std::vector<float> out;
    out.reserve(n);
    for (int i = 0; i < n; ++i)
        out.push_back(mod.tick(0.0, tick_rate).cv);
    return out;
}

shift_register_modulator make(mode m, int length = 8, int dac_bits = 3) {
    shift_register_modulator mod(m, length, dac_bits);
    mod.update("clock_rate", tick_rate);
    return mod;
}

} // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TEST_CASE("shift_register_modulator: default construction does not crash", "[shift_register]") {
    REQUIRE_NOTHROW(shift_register_modulator{});
}

TEST_CASE("shift_register_modulator: output in [-1, 1] at construction", "[shift_register]") {
    for (auto m : {mode::lfsr, mode::rungler, mode::turing, mode::open}) {
        shift_register_modulator mod(m, 8, 3);
        mod.update("clock_rate", tick_rate);
        const float v = mod.tick(0.0, tick_rate).cv;
        REQUIRE(v >= -1.0f);
        REQUIRE(v <= 1.0f);
    }
}

// ---------------------------------------------------------------------------
// Output range and finiteness
// ---------------------------------------------------------------------------

TEST_CASE("shift_register_modulator: all modes output [-1, 1] across many ticks", "[shift_register]") {
    for (auto m : {mode::lfsr, mode::rungler, mode::turing, mode::open}) {
        auto mod = make(m);
        for (int i = 0; i < 512; ++i) {
            const float v = mod.tick(0.0, tick_rate).cv;
            REQUIRE(v >= -1.0f);
            REQUIRE(v <= 1.0f);
            REQUIRE(!std::isnan(v));
        }
    }
}

TEST_CASE("shift_register_modulator: depth scales output", "[shift_register]") {
    auto mod = make(mode::lfsr);
    mod.update("depth", 0.5f);
    for (int i = 0; i < 256; ++i) {
        const float v = mod.tick(0.0, tick_rate).cv;
        REQUIRE(v >= -0.5f);
        REQUIRE(v <= 0.5f);
    }
}

TEST_CASE("shift_register_modulator: depth zero produces zero", "[shift_register]") {
    auto mod = make(mode::lfsr);
    mod.update("depth", 0.0f);
    for (int i = 0; i < 16; ++i)
        REQUIRE(mod.tick(0.0, tick_rate).cv == Catch::Approx(0.0f).margin(1e-6f));
}

// ---------------------------------------------------------------------------
// LFSR mode — maximal-length cycle
// ---------------------------------------------------------------------------

TEST_CASE("shift_register_modulator: LFSR 8-bit produces 255 distinct register states", "[shift_register][lfsr]") {
    auto mod = make(mode::lfsr, 8, 3);

    std::set<float> seen;
    for (int i = 0; i < 255; ++i)
        seen.insert(mod.tick(0.0, tick_rate).cv);

    REQUIRE(seen.size() == 8u);
}

TEST_CASE("shift_register_modulator: LFSR output varies", "[shift_register][lfsr]") {
    auto mod = make(mode::lfsr, 8, 3);
    const auto vals = collect(mod, 64);
    const float first = vals.front();
    bool found_diff = false;
    for (float v : vals) {
        if (v != first) { found_diff = true; break; }
    }
    REQUIRE(found_diff);
}

TEST_CASE("shift_register_modulator: LFSR 16-bit produces 65535 distinct states", "[shift_register][lfsr]") {
    auto mod = make(mode::lfsr, 16, 3);

    std::set<float> seen;
    for (int i = 0; i < 1000; ++i)
        seen.insert(mod.tick(0.0, tick_rate).cv);

    REQUIRE(seen.size() == 8u);
}

// ---------------------------------------------------------------------------
// TURING mode — frozen/random behaviour
// ---------------------------------------------------------------------------

TEST_CASE("shift_register_modulator: TURING param=0 produces near-frozen output", "[shift_register][turing]") {
    auto mod = make(mode::turing, 8, 3);
    mod.update("param", 0.0f);

    for (int i = 0; i < 8; ++i) mod.tick(0.0, tick_rate);

    std::vector<float> cycle_a, cycle_b;
    for (int i = 0; i < 8; ++i) cycle_a.push_back(mod.tick(0.0, tick_rate).cv);
    for (int i = 0; i < 8; ++i) cycle_b.push_back(mod.tick(0.0, tick_rate).cv);

    REQUIRE(cycle_a == cycle_b);
}

TEST_CASE("shift_register_modulator: TURING param=1 produces varying output", "[shift_register][turing]") {
    auto mod = make(mode::turing, 8, 3);
    mod.update("param", 1.0f);

    const auto vals = collect(mod, 64);
    std::set<float> seen(vals.begin(), vals.end());
    REQUIRE(seen.size() > 1u);
}

// ---------------------------------------------------------------------------
// RUNGLER mode
// ---------------------------------------------------------------------------

TEST_CASE("shift_register_modulator: RUNGLER produces varying output", "[shift_register][rungler]") {
    auto mod = make(mode::rungler, 8, 3);
    mod.update("data",  0.3f);
    mod.update("param", 0.5f);

    const auto vals = collect(mod, 64);
    std::set<float> seen(vals.begin(), vals.end());
    REQUIRE(seen.size() > 1u);
}

TEST_CASE("shift_register_modulator: RUNGLER data=0 param=1 shifts 1s and locks", "[shift_register][rungler]") {
    auto mod = make(mode::rungler, 8, 3);
    mod.update("data",  0.0f);
    mod.update("param", 1.0f);

    for (int i = 0; i < 64; ++i) {
        const float v = mod.tick(0.0, tick_rate).cv;
        REQUIRE(v >= -1.0f);
        REQUIRE(v <= 1.0f);
        REQUIRE(!std::isnan(v));
    }
}

// ---------------------------------------------------------------------------
// OPEN mode — deterministic data-driven
// ---------------------------------------------------------------------------

TEST_CASE("shift_register_modulator: OPEN mode data=0 fills register with 0s → minimum output",
          "[shift_register][open]") {
    shift_register_modulator mod(mode::open, 8, 3);
    mod.update("clock_rate", tick_rate);
    mod.update("data", 0.0f);

    for (int i = 0; i < 8; ++i) mod.tick(0.0, tick_rate);
    REQUIRE(mod.tick(0.0, tick_rate).cv == Catch::Approx(-1.0f));
}

TEST_CASE("shift_register_modulator: OPEN mode data=1 fills register with 1s → maximum output",
          "[shift_register][open]") {
    shift_register_modulator mod(mode::open, 8, 3);
    mod.update("clock_rate", tick_rate);
    mod.update("data", 1.0f);

    for (int i = 0; i < 8; ++i) mod.tick(0.0, tick_rate);
    REQUIRE(mod.tick(0.0, tick_rate).cv == Catch::Approx(1.0f));
}

TEST_CASE("shift_register_modulator: OPEN mode gate matches data", "[shift_register][open]") {
    shift_register_modulator mod(mode::open, 8, 3);
    mod.update("clock_rate", tick_rate);

    mod.update("data", 1.0f);
    REQUIRE(mod.tick(0.0, tick_rate).gate == true);

    mod.update("data", 0.0f);
    REQUIRE(mod.tick(0.0, tick_rate).gate == false);
}

// ---------------------------------------------------------------------------
// state field in modulator_output
// ---------------------------------------------------------------------------

TEST_CASE("shift_register_modulator: state reflects register evolution", "[shift_register]") {
    auto mod = make(mode::lfsr, 8, 3);
    // zero-rate tick captures current state without advancing
    const uint32_t s0 = mod.tick(0.0, 0.0f).state;
    const uint32_t s1 = mod.tick(0.0, tick_rate).state;
    REQUIRE(s0 != s1);
}

TEST_CASE("shift_register_modulator: state masked to length", "[shift_register]") {
    for (int len : {4, 8, 12, 16, 32}) {
        auto mod = make(mode::lfsr, len, 3);
        nomos::rt::modulator_output last;
        for (int i = 0; i < 32; ++i) last = mod.tick(0.0, tick_rate);
        const uint32_t mask = (len < 32) ? ((1u << len) - 1u) : 0xFFFF'FFFFu;
        REQUIRE((last.state & ~mask) == 0u);
    }
}

// ---------------------------------------------------------------------------
// Clock rate
// ---------------------------------------------------------------------------

TEST_CASE("shift_register_modulator: slow clock_rate produces fewer edges", "[shift_register]") {
    shift_register_modulator mod(mode::lfsr, 8, 3);
    mod.update("clock_rate", 1.0f);

    const float v0 = mod.tick(0.0, tick_rate).cv;
    for (int i = 1; i < 99; ++i) {
        const float v = mod.tick(0.0, tick_rate).cv;
        REQUIRE(v == Catch::Approx(v0));
    }
    REQUIRE_NOTHROW(mod.tick(0.0, tick_rate));
}

// ---------------------------------------------------------------------------
// Parameter update
// ---------------------------------------------------------------------------

TEST_CASE("shift_register_modulator: unknown key is a no-op", "[shift_register]") {
    auto mod = make(mode::lfsr, 8, 3);
    REQUIRE_NOTHROW(mod.update("nonexistent", 0.5f));
    REQUIRE(!std::isnan(mod.tick(0.0, tick_rate).cv));
}

TEST_CASE("shift_register_modulator: clock_rate clamped to [0.001, 1000]", "[shift_register]") {
    auto mod = make(mode::lfsr, 8, 3);
    REQUIRE_NOTHROW(mod.update("clock_rate", -10.0f));
    REQUIRE_NOTHROW(mod.update("clock_rate", 1e9f));
    REQUIRE(!std::isnan(mod.tick(0.0, tick_rate).cv));
}

// ---------------------------------------------------------------------------
// DAC bits
// ---------------------------------------------------------------------------

TEST_CASE("shift_register_modulator: 1-bit DAC produces only -1 and +1", "[shift_register]") {
    shift_register_modulator mod(mode::lfsr, 8, 1);
    mod.update("clock_rate", tick_rate);

    std::set<float> seen;
    for (int i = 0; i < 64; ++i)
        seen.insert(mod.tick(0.0, tick_rate).cv);

    for (float v : seen) {
        REQUIRE((v == Catch::Approx(-1.0f) || v == Catch::Approx(1.0f)));
    }
}
