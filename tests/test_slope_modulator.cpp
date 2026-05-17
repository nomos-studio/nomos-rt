// SPDX-License-Identifier: LGPL-2.1-or-later
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "slope_modulator.hpp"

#include <cmath>
#include <limits>

TEST_CASE("slope_modulator: bipolar output in [-1, 1]", "[slope_modulator]") {
    nomos::rt::slope_modulator mod;
    constexpr float tick_rate = 100.0f;
    constexpr int   n_ticks   = 200;

    for (int i = 0; i < n_ticks; ++i) {
        const float v = mod.tick(static_cast<double>(i) * 0.01, tick_rate);
        REQUIRE(v >= -1.0f);
        REQUIRE(v <= 1.0f);
        REQUIRE(!std::isnan(v));
        REQUIRE(!std::isinf(v));
    }
}

TEST_CASE("slope_modulator: unipolar output in [0, 1]", "[slope_modulator]") {
    nomos::rt::slope_modulator mod;
    mod.update("bipolar", 0.0f);

    constexpr float tick_rate = 100.0f;
    constexpr int   n_ticks   = 200;

    for (int i = 0; i < n_ticks; ++i) {
        const float v = mod.tick(static_cast<double>(i) * 0.01, tick_rate);
        REQUIRE(v >= 0.0f);
        REQUIRE(v <= 1.0f);
        REQUIRE(!std::isnan(v));
    }
}

TEST_CASE("slope_modulator: depth scales output", "[slope_modulator]") {
    nomos::rt::slope_modulator mod;
    mod.update("depth", 0.5f);

    constexpr float tick_rate = 100.0f;
    constexpr int   n_ticks   = 200;

    for (int i = 0; i < n_ticks; ++i) {
        const float v = mod.tick(static_cast<double>(i) * 0.01, tick_rate);
        REQUIRE(v >= -0.5f);
        REQUIRE(v <= 0.5f);
    }
}

TEST_CASE("slope_modulator: depth zero produces near-zero output", "[slope_modulator]") {
    nomos::rt::slope_modulator mod;
    mod.update("depth", 0.0f);

    const float v = mod.tick(0.0, 100.0f);
    REQUIRE(v == Catch::Approx(0.0f).margin(1e-6f));
}

TEST_CASE("slope_modulator: rate clamped to [0.001, 100]", "[slope_modulator]") {
    nomos::rt::slope_modulator mod;

    // These should not crash and should produce finite values.
    mod.update("rate", -5.0f);   // clamped to 0.001
    REQUIRE(!std::isnan(mod.tick(0.0, 100.0f)));

    mod.update("rate", 500.0f);  // clamped to 100.0
    REQUIRE(!std::isnan(mod.tick(0.0, 100.0f)));
}

TEST_CASE("slope_modulator: shape param accepted in [-1, 1]", "[slope_modulator]") {
    nomos::rt::slope_modulator mod;
    // Should not crash at extremes.
    mod.update("shape", -1.0f);
    REQUIRE(!std::isnan(mod.tick(0.0, 100.0f)));
    mod.update("shape", 1.0f);
    REQUIRE(!std::isnan(mod.tick(0.0, 100.0f)));
}

TEST_CASE("slope_modulator: slope param accepted in [-1, 1]", "[slope_modulator]") {
    nomos::rt::slope_modulator mod;
    mod.update("slope", -1.0f);
    REQUIRE(!std::isnan(mod.tick(0.0, 100.0f)));
    mod.update("slope", 1.0f);
    REQUIRE(!std::isnan(mod.tick(0.0, 100.0f)));
}

TEST_CASE("slope_modulator: smoothness param accepted in [-1, 1]", "[slope_modulator]") {
    nomos::rt::slope_modulator mod;
    mod.update("smoothness", -1.0f);
    REQUIRE(!std::isnan(mod.tick(0.0, 100.0f)));
    mod.update("smoothness", 1.0f);
    REQUIRE(!std::isnan(mod.tick(0.0, 100.0f)));
}

TEST_CASE("slope_modulator: unknown key is a no-op", "[slope_modulator]") {
    nomos::rt::slope_modulator mod;
    REQUIRE_NOTHROW(mod.update("nonexistent", 0.5f));
    REQUIRE(!std::isnan(mod.tick(0.0, 100.0f)));
}

TEST_CASE("slope_modulator: output varies across ticks (LFO advances)", "[slope_modulator]") {
    // At 1Hz rate with 100Hz tick rate, one full cycle takes 100 ticks.
    // Outputs should not all be identical.
    nomos::rt::slope_modulator mod;
    mod.update("rate", 1.0f);

    constexpr float tick_rate = 100.0f;
    float first = mod.tick(0.0, tick_rate);
    bool  found_different = false;

    for (int i = 1; i < 100; ++i) {
        const float v = mod.tick(static_cast<double>(i) / tick_rate, tick_rate);
        if (std::abs(v - first) > 1e-3f) {
            found_different = true;
            break;
        }
    }
    REQUIRE(found_different);
}

TEST_CASE("slope_modulator: bipolar/unipolar switch mid-stream", "[slope_modulator]") {
    nomos::rt::slope_modulator mod;

    // Warm up with bipolar.
    for (int i = 0; i < 10; ++i)
        mod.tick(static_cast<double>(i) * 0.01, 100.0f);

    // Switch to unipolar — all subsequent values must be non-negative.
    mod.update("bipolar", 0.0f);
    for (int i = 10; i < 50; ++i) {
        const float v = mod.tick(static_cast<double>(i) * 0.01, 100.0f);
        REQUIRE(v >= 0.0f);
        REQUIRE(v <= 1.0f);
    }
}
