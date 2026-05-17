// SPDX-License-Identifier: LGPL-2.1-or-later
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "segment_modulator.hpp"

#include <cmath>
#include <vector>

using nomos::rt::segment_modulator;
using seg_type = segment_modulator::type;

namespace {

segment_modulator make_single_ramp() {
    return segment_modulator({{
        seg_type::ramp,
        /*primary=*/   0.5f,
        /*secondary=*/ 0.5f,
        /*loop=*/      true,
    }});
}

} // namespace

TEST_CASE("segment_modulator: output in [0, 1]", "[segment_modulator]") {
    auto mod = make_single_ramp();

    constexpr int n_ticks = 200;
    for (int i = 0; i < n_ticks; ++i) {
        const float v = mod.tick(static_cast<double>(i) * 0.01, 100.0f);
        REQUIRE(v >= 0.0f);
        REQUIRE(v <= 1.0f);
        REQUIRE(!std::isnan(v));
        REQUIRE(!std::isinf(v));
    }
}

TEST_CASE("segment_modulator: depth scales output", "[segment_modulator]") {
    auto mod = make_single_ramp();
    mod.update("depth", 0.5f);

    constexpr int n_ticks = 200;
    for (int i = 0; i < n_ticks; ++i) {
        const float v = mod.tick(static_cast<double>(i) * 0.01, 100.0f);
        REQUIRE(v >= 0.0f);
        REQUIRE(v <= 0.5f);
    }
}

TEST_CASE("segment_modulator: depth zero produces near-zero output", "[segment_modulator]") {
    auto mod = make_single_ramp();
    mod.update("depth", 0.0f);

    const float v = mod.tick(0.0, 100.0f);
    REQUIRE(v == Catch::Approx(0.0f).margin(1e-6f));
}

TEST_CASE("segment_modulator: update segment_0_primary does not crash", "[segment_modulator]") {
    auto mod = make_single_ramp();
    REQUIRE_NOTHROW(mod.update("segment_0_primary", 0.8f));
    REQUIRE(!std::isnan(mod.tick(0.0, 100.0f)));
}

TEST_CASE("segment_modulator: update segment_0_secondary does not crash", "[segment_modulator]") {
    auto mod = make_single_ramp();
    REQUIRE_NOTHROW(mod.update("segment_0_secondary", 0.2f));
    REQUIRE(!std::isnan(mod.tick(0.0, 100.0f)));
}

TEST_CASE("segment_modulator: segment param clamped to [0, 1]", "[segment_modulator]") {
    auto mod = make_single_ramp();
    REQUIRE_NOTHROW(mod.update("segment_0_primary", -0.5f));
    REQUIRE(!std::isnan(mod.tick(0.0, 100.0f)));
    REQUIRE_NOTHROW(mod.update("segment_0_primary", 1.5f));
    REQUIRE(!std::isnan(mod.tick(0.0, 100.0f)));
}

TEST_CASE("segment_modulator: out-of-range segment index is a no-op", "[segment_modulator]") {
    auto mod = make_single_ramp();
    REQUIRE_NOTHROW(mod.update("segment_5_primary", 0.5f));
    REQUIRE(!std::isnan(mod.tick(0.0, 100.0f)));
}

TEST_CASE("segment_modulator: unknown key is a no-op", "[segment_modulator]") {
    auto mod = make_single_ramp();
    REQUIRE_NOTHROW(mod.update("nonexistent", 0.5f));
    REQUIRE(!std::isnan(mod.tick(0.0, 100.0f)));
}

TEST_CASE("segment_modulator: multi-segment construction does not crash", "[segment_modulator]") {
    const std::vector<segment_modulator::segment_def> defs = {
        {seg_type::ramp, 0.3f, 0.5f, false},
        {seg_type::hold, 0.7f, 0.5f, false},
        {seg_type::ramp, 0.5f, 0.5f, true},
    };
    segment_modulator mod(defs);

    constexpr int n_ticks = 100;
    for (int i = 0; i < n_ticks; ++i) {
        const float v = mod.tick(static_cast<double>(i) * 0.01, 100.0f);
        REQUIRE(v >= 0.0f);
        REQUIRE(v <= 1.0f);
        REQUIRE(!std::isnan(v));
    }
}

TEST_CASE("segment_modulator: step segment output in [0, 1]", "[segment_modulator]") {
    const std::vector<segment_modulator::segment_def> defs = {
        {seg_type::step, 0.8f, 0.5f, true},
    };
    segment_modulator mod(defs);

    for (int i = 0; i < 50; ++i) {
        const float v = mod.tick(static_cast<double>(i) * 0.01, 100.0f);
        REQUIRE(v >= 0.0f);
        REQUIRE(v <= 1.0f);
    }
}

TEST_CASE("segment_modulator: alt segment alternates between primary and secondary", "[segment_modulator]") {
    const std::vector<segment_modulator::segment_def> defs = {
        {seg_type::alt, 0.8f, 0.2f, true},
    };
    segment_modulator mod(defs);
    mod.update("rate", 1.0f);

    // Run more than two full cycles; collect all distinct values.
    bool saw_08 = false, saw_02 = false;
    for (int i = 0; i < 300; ++i) {
        const float v = mod.tick(0.0, 100.0f);
        if (v >= 0.79f) saw_08 = true;
        if (v <= 0.21f) saw_02 = true;
    }
    REQUIRE(saw_08);
    REQUIRE(saw_02);
}

TEST_CASE("segment_modulator: ramp segment varies within cycle", "[segment_modulator]") {
    const std::vector<segment_modulator::segment_def> defs = {
        {seg_type::ramp, 1.0f, 0.5f, true},
    };
    segment_modulator mod(defs);
    mod.update("rate", 1.0f);

    float min_v = 1.0f, max_v = 0.0f;
    for (int i = 0; i < 200; ++i) {
        const float v = mod.tick(0.0, 100.0f);
        if (v < min_v) min_v = v;
        if (v > max_v) max_v = v;
    }
    REQUIRE(max_v > min_v + 0.1f);
}
