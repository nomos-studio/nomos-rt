// SPDX-License-Identifier: LGPL-2.1-or-later
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "genie_modulator.hpp"

using nomos::rt::genie_modulator;

namespace {
constexpr float rate = 375.0f;
} // namespace

TEST_CASE("genie: default construction does not crash", "[genie]") {
    REQUIRE_NOTHROW(genie_modulator{});
}

TEST_CASE("genie: outputs are in [0, 1]", "[genie]") {
    genie_modulator m;
    for (int i = 0; i < 50; ++i) {
        const auto out = m.tick(0.0, rate);
        REQUIRE(out.cv  >= 0.0f); REQUIRE(out.cv  <= 1.0f);
        REQUIRE(out.aux >= 0.0f); REQUIRE(out.aux <= 1.0f);
        for (int j = 0; j < 3; ++j) {
            REQUIRE(out.outputs[j] >= 0.0f);
            REQUIRE(out.outputs[j] <= 1.0f);
        }
    }
}

TEST_CASE("genie: cv mirrors outputs[0]", "[genie]") {
    genie_modulator m;
    for (int i = 0; i < 20; ++i) {
        const auto out = m.tick(0.0, rate);
        REQUIRE(out.cv == Catch::Approx(out.outputs[0]));
    }
}

TEST_CASE("genie: state bitmap matches per-neuron sign", "[genie]") {
    genie_modulator m;
    for (int i = 0; i < 20; ++i) {
        const auto out = m.tick(0.0, rate);
        // state bit i = 1 iff neuron i output > 0 (before normalisation).
        // We can verify indirectly: if outputs[i] > 0.5 (i.e. above neutral),
        // the raw value > 0, so bit i should be set.
        for (int j = 0; j < 3; ++j) {
            const bool bit_set = (out.state >> j) & 1u;
            // Normalised output 0.5 corresponds to raw output = 0 for response=3.
            // Above 0.5 → raw > 0 → bit should be 1.
            if (out.outputs[j] > 0.5f + 0.01f) REQUIRE(bit_set);
            if (out.outputs[j] < 0.5f - 0.01f) REQUIRE_FALSE(bit_set);
        }
    }
}

TEST_CASE("genie: gate and gate2 are mutually exclusive or both false", "[genie]") {
    genie_modulator m;
    for (int i = 0; i < 50; ++i) {
        const auto out = m.tick(0.0, rate);
        REQUIRE_FALSE((out.gate && out.gate2));
    }
}

TEST_CASE("genie: ring evolves — output is not constant", "[genie]") {
    // At moderate gain (default 0.6) the ring should oscillate.
    genie_modulator m;
    const float first = m.tick(0.0, rate).cv;
    bool changed = false;
    for (int i = 0; i < 100; ++i)
        if (m.tick(0.0, rate).cv != first) { changed = true; break; }
    REQUIRE(changed);
}

TEST_CASE("genie: low gain converges (frozen output)", "[genie]") {
    genie_modulator m;
    for (int i = 0; i < 3; ++i) m.update(std::string("gain") + char('0'+i), 0.0f);
    // With gain=0 each neuron only sees sense (no ring signal) → same every tick.
    const float first = m.tick(0.0, rate).cv;
    for (int i = 0; i < 20; ++i)
        REQUIRE(m.tick(0.0, rate).cv == Catch::Approx(first));
}

TEST_CASE("genie: N=2 construction does not crash", "[genie]") {
    REQUIRE_NOTHROW(genie_modulator{2});
    genie_modulator m(2);
    REQUIRE_NOTHROW(m.tick(0.0, rate));
}

TEST_CASE("genie: N=5 fills outputs[0..4]", "[genie]") {
    genie_modulator m(5);
    const auto out = m.tick(0.0, rate);
    for (int i = 0; i < 5; ++i) {
        REQUIRE(out.outputs[i] >= 0.0f);
        REQUIRE(out.outputs[i] <= 1.0f);
    }
    // outputs[5..7] remain zero-initialised.
    for (int i = 5; i < 8; ++i)
        REQUIRE(out.outputs[i] == Catch::Approx(0.0f));
}

TEST_CASE("genie: sense/response/gain update accepted without crash", "[genie]") {
    genie_modulator m;
    for (int i = 0; i < 3; ++i) {
        m.update(std::string("sense")    + char('0'+i), 1.5f);
        m.update(std::string("response") + char('0'+i), 5.0f);
        m.update(std::string("gain")     + char('0'+i), 0.4f);
    }
    REQUIRE_NOTHROW(m.tick(0.0, rate));
}

TEST_CASE("genie: unknown update key is a no-op", "[genie]") {
    genie_modulator m;
    REQUIRE_NOTHROW(m.update("bogus", 1.0f));
    REQUIRE_NOTHROW(m.tick(0.0, rate));
}
