// SPDX-License-Identifier: LGPL-2.1-or-later
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "sloth_chaos_modulator.hpp"

using nomos::rt::sloth_chaos_modulator;
using variant = sloth_chaos_modulator::variant;

namespace {
constexpr float rate = 375.0f;
} // namespace

TEST_CASE("sloth: default construction does not crash", "[sloth]") {
    REQUIRE_NOTHROW(sloth_chaos_modulator{});
}

TEST_CASE("sloth: outputs are in [0, 1]", "[sloth]") {
    sloth_chaos_modulator m;
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

TEST_CASE("sloth: cv mirrors outputs[0], aux mirrors outputs[1]", "[sloth]") {
    sloth_chaos_modulator m;
    for (int i = 0; i < 20; ++i) {
        const auto out = m.tick(0.0, rate);
        REQUIRE(out.cv  == Catch::Approx(out.outputs[0]));
        REQUIRE(out.aux == Catch::Approx(out.outputs[1]));
    }
}

TEST_CASE("sloth: gate reports current lobe (z > 0)", "[sloth]") {
    sloth_chaos_modulator m;
    for (int i = 0; i < 50; ++i) {
        const auto out = m.tick(0.0, rate);
        // outputs[3] = 1.0 when z > 0 (positive lobe).
        const bool lobe_from_outputs = out.outputs[3] > 0.5f;
        REQUIRE(out.gate == lobe_from_outputs);
        REQUIRE(out.state == (out.gate ? 1u : 0u));
    }
}

TEST_CASE("sloth: gate2 fires only on lobe transitions", "[sloth]") {
    sloth_chaos_modulator m;
    bool last_gate = m.tick(0.0, rate).gate;
    int transitions_via_gate2 = 0;
    int natural_transitions    = 0;
    for (int i = 0; i < 500; ++i) {
        const auto out = m.tick(0.0, rate);
        if (out.gate2) ++transitions_via_gate2;
        if (out.gate != last_gate) ++natural_transitions;
        last_gate = out.gate;
    }
    // gate2 count must equal the number of actual gate transitions.
    REQUIRE(transitions_via_gate2 == natural_transitions);
}

TEST_CASE("sloth: state evolves — cv is not constant over many ticks", "[sloth]") {
    sloth_chaos_modulator m;
    float first = m.tick(0.0, rate).cv;
    bool  changed = false;
    for (int i = 0; i < 200; ++i)
        if (m.tick(0.0, rate).cv != first) { changed = true; break; }
    REQUIRE(changed);
}

TEST_CASE("sloth: knob=0 vs knob=1 produce different trajectories", "[sloth]") {
    sloth_chaos_modulator m0, m1;
    m0.update("knob", 0.0f);
    m1.update("knob", 1.0f);
    bool saw_difference = false;
    for (int i = 0; i < 200; ++i) {
        const auto o0 = m0.tick(0.0, rate);
        const auto o1 = m1.tick(0.0, rate);
        if (o0.state != o1.state) { saw_difference = true; break; }
    }
    REQUIRE(saw_difference);
}

TEST_CASE("sloth: apathy runs slower than torpor", "[sloth]") {
    // Torpor and apathy start from the same initial state.
    // After the same number of ticks, torpor (faster) should be further
    // along its orbit than apathy — compare accumulated x displacement.
    sloth_chaos_modulator torpor(variant::torpor);
    sloth_chaos_modulator apathy(variant::apathy);

    float torpor_range = 0.0f, apathy_range = 0.0f;
    float tmin = 1.0f, tmax = 0.0f, amin = 1.0f, amax = 0.0f;
    for (int i = 0; i < 200; ++i) {
        float tv = torpor.tick(0.0, rate).cv;
        float av = apathy.tick(0.0, rate).cv;
        tmin = std::min(tmin, tv); tmax = std::max(tmax, tv);
        amin = std::min(amin, av); amax = std::max(amax, av);
    }
    torpor_range = tmax - tmin;
    apathy_range = amax - amin;
    // Torpor explores more of its orbit in the same wall-clock ticks.
    REQUIRE(torpor_range > apathy_range);
}

TEST_CASE("sloth: triple variant produces 7 filled outputs", "[sloth]") {
    sloth_chaos_modulator m(variant::triple);
    const auto out = m.tick(0.0, rate);
    // All 7 filled slots should be valid (outputs[0..6]).
    for (int i = 0; i < 7; ++i) {
        REQUIRE(out.outputs[i] >= 0.0f);
        REQUIRE(out.outputs[i] <= 1.0f);
    }
    // gate = torpor lobe indicator; state encodes all three.
    REQUIRE((out.state & ~0x7u) == 0u);  // only bits 0-2 used
}

TEST_CASE("sloth: unknown update key is a no-op", "[sloth]") {
    sloth_chaos_modulator m;
    REQUIRE_NOTHROW(m.update("bogus", 1.0f));
    REQUIRE_NOTHROW(m.tick(0.0, rate));
}
