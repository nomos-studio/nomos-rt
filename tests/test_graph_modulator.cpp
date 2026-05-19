// SPDX-License-Identifier: LGPL-2.1-or-later
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nomos/rt/modulator_engine.hpp>
#include "graph_modulator.hpp"

#include <edn/parser.hpp>

#include <algorithm>
#include <map>
#include <string>

namespace {

// Parse an EDN string and build a control_graph; fails the test on parse error.
nomos::rt::control_graph parse(const char* edn_str,
                                std::unordered_map<std::string, float> params = {}) {
    auto r = edn::parse(std::string_view{edn_str});
    REQUIRE(r.has_value());
    return nomos::rt::parse_control_graph(*r, std::move(params));
}

} // namespace

// ---------------------------------------------------------------------------
// Leaf nodes
// ---------------------------------------------------------------------------

TEST_CASE("graph_modulator: literal float output", "[graph_modulator]") {
    nomos::rt::graph_modulator m{parse("0.42")};
    REQUIRE(m.tick(0.0, 100.0f).cv == Catch::Approx(0.42f));
}

TEST_CASE("graph_modulator: const node", "[graph_modulator]") {
    nomos::rt::graph_modulator m{parse("[:const 0.7]")};
    REQUIRE(m.tick(0.0, 100.0f).cv == Catch::Approx(0.7f));
}

TEST_CASE("graph_modulator: param_ref reads from params map", "[graph_modulator]") {
    nomos::rt::graph_modulator m{parse("[:param :gain]", {{"gain", 0.6f}})};
    REQUIRE(m.tick(0.0, 100.0f).cv == Catch::Approx(0.6f));
}

TEST_CASE("graph_modulator: param_ref returns zero when key absent", "[graph_modulator]") {
    nomos::rt::graph_modulator m{parse("[:param :missing]")};
    REQUIRE(m.tick(0.0, 100.0f).cv == Catch::Approx(0.0f));
}

TEST_CASE("graph_modulator: update changes param mid-run", "[graph_modulator]") {
    nomos::rt::graph_modulator m{parse("[:param :depth]", {{"depth", 0.1f}})};
    REQUIRE(m.tick(0.0, 100.0f).cv == Catch::Approx(0.1f));
    m.update("depth", 0.9f);
    REQUIRE(m.tick(0.0, 100.0f).cv == Catch::Approx(0.9f));
}

// ---------------------------------------------------------------------------
// beat_phase — musically-locked phase (Q32)
// ---------------------------------------------------------------------------

TEST_CASE("graph_modulator: beat_phase is 0 at beat 0", "[graph_modulator]") {
    nomos::rt::graph_modulator m{parse("[:beat-phase 4.0]")};
    REQUIRE(m.tick(0.0, 100.0f).cv == Catch::Approx(0.0f));
}

TEST_CASE("graph_modulator: beat_phase is 0.25 at beat 1 of 4", "[graph_modulator]") {
    nomos::rt::graph_modulator m{parse("[:beat-phase 4.0]")};
    REQUIRE(m.tick(1.0, 100.0f).cv == Catch::Approx(0.25f));
}

TEST_CASE("graph_modulator: beat_phase is 0.5 at beat 2 of 4", "[graph_modulator]") {
    nomos::rt::graph_modulator m{parse("[:beat-phase 4.0]")};
    REQUIRE(m.tick(2.0, 100.0f).cv == Catch::Approx(0.5f));
}

TEST_CASE("graph_modulator: beat_phase wraps at period boundary", "[graph_modulator]") {
    nomos::rt::graph_modulator m{parse("[:beat-phase 4.0]")};
    REQUIRE(m.tick(4.0, 100.0f).cv == Catch::Approx(0.0f));
    REQUIRE(m.tick(5.0, 100.0f).cv == Catch::Approx(0.25f));
    REQUIRE(m.tick(8.0, 100.0f).cv == Catch::Approx(0.0f));
}

TEST_CASE("graph_modulator: beat_phase coherent across BPM change", "[graph_modulator]") {
    // Phase is derived from beat, not wall clock — same beat gives same phase
    // regardless of how fast or slow ticks arrive.
    nomos::rt::graph_modulator m{parse("[:beat-phase 4.0]")};
    // Slow tempo: tick_rate_hz = 10
    REQUIRE(m.tick(2.0, 10.0f).cv == Catch::Approx(0.5f));
    // Fast tempo: tick_rate_hz = 400 — same beat, same phase
    REQUIRE(m.tick(2.0, 400.0f).cv == Catch::Approx(0.5f));
}

TEST_CASE("graph_modulator: beat_phase with period=1 cycles every beat", "[graph_modulator]") {
    nomos::rt::graph_modulator m{parse("[:beat-phase 1.0]")};
    REQUIRE(m.tick(0.0,  100.0f).cv == Catch::Approx(0.0f));
    REQUIRE(m.tick(0.25, 100.0f).cv == Catch::Approx(0.25f));
    REQUIRE(m.tick(0.5,  100.0f).cv == Catch::Approx(0.5f));
    REQUIRE(m.tick(0.75, 100.0f).cv == Catch::Approx(0.75f));
    REQUIRE(m.tick(1.0,  100.0f).cv == Catch::Approx(0.0f));
}

TEST_CASE("graph_modulator: beat_phase with param-controlled period", "[graph_modulator]") {
    nomos::rt::graph_modulator m{parse("[:beat-phase [:param :period]]",
                                       {{"period", 2.0f}})};
    REQUIRE(m.tick(1.0, 100.0f).cv == Catch::Approx(0.5f));
    m.update("period", 4.0f);
    REQUIRE(m.tick(1.0, 100.0f).cv == Catch::Approx(0.25f));
}

TEST_CASE("graph_modulator: beat_phase zero period returns 0", "[graph_modulator]") {
    nomos::rt::graph_modulator m{parse("[:beat-phase 0.0]")};
    REQUIRE(m.tick(1.0, 100.0f).cv == Catch::Approx(0.0f));
}

TEST_CASE("graph_modulator: sin of beat_phase sweeps full range", "[graph_modulator]") {
    // [:sin [:beat-phase 4.0]] driven by beat 0..4 should cover the sine range
    nomos::rt::graph_modulator m{parse("[:sin [:beat-phase 4.0]]")};
    float lo = 1.0f, hi = -1.0f;
    for (int i = 0; i <= 100; ++i) {
        float v = m.tick(4.0 * i / 100.0, 100.0f).cv;
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    REQUIRE(lo < -0.9f);
    REQUIRE(hi >  0.9f);
}

// ---------------------------------------------------------------------------
// Oscillators
// ---------------------------------------------------------------------------

TEST_CASE("graph_modulator: phasor advances phase over ticks", "[graph_modulator]") {
    // rate=1 Hz at 100 Hz tick_rate → phase advances 0.01/tick → 100 ticks = 1 cycle
    nomos::rt::graph_modulator m{parse("[:phasor 1.0]")};
    float prev = m.tick(0.0, 100.0f).cv;
    for (int i = 0; i < 10; ++i) {
        float cur = m.tick(0.0, 100.0f).cv;
        REQUIRE(cur > prev);
        prev = cur;
    }
}

TEST_CASE("graph_modulator: phasor wraps at 1", "[graph_modulator]") {
    nomos::rt::graph_modulator m{parse("[:phasor 1.0]")};
    float max_phase = 0.0f;
    for (int i = 0; i < 110; ++i)
        max_phase = std::max(max_phase, m.tick(0.0, 100.0f).cv);
    REQUIRE(max_phase < 1.0f);
}

TEST_CASE("graph_modulator: sin of phasor sweeps full bipolar range", "[graph_modulator]") {
    nomos::rt::graph_modulator m{parse("[:sin [:phasor 1.0]]")};
    float lo = 1.0f, hi = -1.0f;
    for (int i = 0; i < 200; ++i) {
        float v = m.tick(0.0, 100.0f).cv;
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    REQUIRE(lo < -0.9f);
    REQUIRE(hi >  0.9f);
}

TEST_CASE("graph_modulator: tri wave stays in [-1, 1]", "[graph_modulator]") {
    nomos::rt::graph_modulator m{parse("[:tri [:phasor 1.0]]")};
    for (int i = 0; i < 200; ++i) {
        float v = m.tick(0.0, 100.0f).cv;
        REQUIRE(v >= -1.0f);
        REQUIRE(v <=  1.0f);
    }
}

TEST_CASE("graph_modulator: saw is monotone then resets", "[graph_modulator]") {
    nomos::rt::graph_modulator m{parse("[:saw [:phasor 1.0]]")};
    float prev = m.tick(0.0, 100.0f).cv;
    bool  saw_reset = false;
    for (int i = 0; i < 110; ++i) {
        float cur = m.tick(0.0, 100.0f).cv;
        if (cur < prev) saw_reset = true;
        prev = cur;
    }
    REQUIRE(saw_reset);
}

// ---------------------------------------------------------------------------
// Math nodes
// ---------------------------------------------------------------------------

TEST_CASE("graph_modulator: scale maps 0..1 to custom range", "[graph_modulator]") {
    // [:scale 0.5 -1.0 1.0] → -1 + 0.5*2 = 0.0
    nomos::rt::graph_modulator m{parse("[:scale 0.5 -1.0 1.0]")};
    REQUIRE(m.tick(0.0, 100.0f).cv == Catch::Approx(0.0f));
}

TEST_CASE("graph_modulator: add sums two inputs", "[graph_modulator]") {
    nomos::rt::graph_modulator m{parse("[:add 0.3 0.4]")};
    REQUIRE(m.tick(0.0, 100.0f).cv == Catch::Approx(0.7f));
}

TEST_CASE("graph_modulator: mul multiplies two inputs", "[graph_modulator]") {
    nomos::rt::graph_modulator m{parse("[:mul 0.5 0.8]")};
    REQUIRE(m.tick(0.0, 100.0f).cv == Catch::Approx(0.4f));
}

TEST_CASE("graph_modulator: neg negates input", "[graph_modulator]") {
    nomos::rt::graph_modulator m{parse("[:neg 0.75]")};
    REQUIRE(m.tick(0.0, 100.0f).cv == Catch::Approx(-0.75f));
}

TEST_CASE("graph_modulator: clamp constrains output", "[graph_modulator]") {
    nomos::rt::graph_modulator m{parse("[:clamp 1.5 0.0 1.0]")};
    REQUIRE(m.tick(0.0, 100.0f).cv == Catch::Approx(1.0f));
}

TEST_CASE("graph_modulator: mix interpolates between two values", "[graph_modulator]") {
    // t=0.5 → midpoint of 0 and 1 = 0.5
    nomos::rt::graph_modulator m{parse("[:mix 0.0 1.0 0.5]")};
    REQUIRE(m.tick(0.0, 100.0f).cv == Catch::Approx(0.5f));
}

// ---------------------------------------------------------------------------
// Dynamics
// ---------------------------------------------------------------------------

TEST_CASE("graph_modulator: slew lags behind a step", "[graph_modulator]") {
    // rise=0.1 s at 100 Hz → 10 ticks to reach target
    // After 5 ticks the output should still be below target
    nomos::rt::graph_modulator m{parse("[:slew 1.0 0.1 0.1]")};
    float v5 = 0.0f;
    for (int i = 0; i < 5; ++i)
        v5 = m.tick(0.0, 100.0f).cv;
    REQUIRE(v5 > 0.0f);
    REQUIRE(v5 < 1.0f);
}

TEST_CASE("graph_modulator: slew reaches target after rise time", "[graph_modulator]") {
    nomos::rt::graph_modulator m{parse("[:slew 1.0 0.1 0.1]")};
    float v = 0.0f;
    for (int i = 0; i < 15; ++i)  // 1.5× rise time
        v = m.tick(0.0, 100.0f).cv;
    REQUIRE(v == Catch::Approx(1.0f));
}

TEST_CASE("graph_modulator: sample_hold captures on rising edge", "[graph_modulator]") {
    // signal=0.8, gate starts low; trigger once by evaluating with high gate
    // We can't easily drive a dynamic gate from a test, so build the graph manually.
    nomos::rt::control_graph g;

    // node 0: const signal 0.8
    g.nodes.push_back({.type = nomos::rt::cg_node::kind::const_val, .args = {0.8f}});
    // node 1: const gate 0.0 (will be patched via params)
    g.nodes.push_back({.type = nomos::rt::cg_node::kind::param_ref,
                       .param_key = "gate"});
    // node 2: sample_hold(signal, gate)
    nomos::rt::cg_node sh{.type = nomos::rt::cg_node::kind::sample_hold};
    sh.inputs[0] = 0;
    sh.inputs[1] = 1;
    g.nodes.push_back(sh);

    g.params["gate"] = 0.0f;
    g.out_cv = 2;

    nomos::rt::graph_modulator m{std::move(g)};

    // Before trigger: held = 0 (initial state)
    REQUIRE(m.tick(0.0, 100.0f).cv == Catch::Approx(0.0f));

    // Rising edge
    m.update("gate", 1.0f);
    m.tick(0.0, 100.0f);  // captures 0.8

    // Gate still high: no new capture
    float held = m.tick(0.0, 100.0f).cv;
    REQUIRE(held == Catch::Approx(0.8f));

    // Gate low again: still holds 0.8
    m.update("gate", 0.0f);
    REQUIRE(m.tick(0.0, 100.0f).cv == Catch::Approx(0.8f));
}

// ---------------------------------------------------------------------------
// Gate output
// ---------------------------------------------------------------------------

TEST_CASE("graph_modulator: threshold produces gate output", "[graph_modulator]") {
    // Multi-output form: cv = phasor, gate = threshold(phasor, 0.5)
    nomos::rt::graph_modulator m{parse(
        "{:cv [:phasor 1.0] :gate [:threshold [:phasor 1.0] 0.5]}")};

    // run for a full cycle; gate should be true for the upper half
    int gate_high = 0, gate_low = 0;
    for (int i = 0; i < 100; ++i) {
        auto out = m.tick(0.0, 100.0f);
        if (out.gate) ++gate_high; else ++gate_low;
    }
    REQUIRE(gate_high > 0);
    REQUIRE(gate_low  > 0);
}

// ---------------------------------------------------------------------------
// Cross-modulator references
// ---------------------------------------------------------------------------

TEST_CASE("graph_modulator: mod-out reads preceding modulator in same tick",
          "[graph_modulator]") {
    nomos::rt::modulator_engine eng;

    // src started first → ticks first; output recorded before sink ticks
    eng.start("src",  std::make_unique<nomos::rt::graph_modulator>(parse("0.75"), &eng));
    eng.start("sink", std::make_unique<nomos::rt::graph_modulator>(
        parse("[:mod-out :src :cv]"), &eng));

    std::map<std::string, float> results;
    eng.tick(0.0, 100.0f, [&](const std::string& id, const nomos::rt::modulator_output& out) {
        results[id] = out.cv;
    });

    REQUIRE(results["src"]  == Catch::Approx(0.75f));
    REQUIRE(results["sink"] == Catch::Approx(0.75f));
}

TEST_CASE("graph_modulator: mod-out on unknown id returns zero", "[graph_modulator]") {
    nomos::rt::modulator_engine eng;
    eng.start("m", std::make_unique<nomos::rt::graph_modulator>(
        parse("[:mod-out :nonexistent :cv]"), &eng));

    float cv = 0.0f;
    eng.tick(0.0, 100.0f, [&](const std::string&, const nomos::rt::modulator_output& out) {
        cv = out.cv;
    });
    REQUIRE(cv == Catch::Approx(0.0f));
}

TEST_CASE("graph_modulator: self-referential mod-out gives one-tick feedback",
          "[graph_modulator]") {
    // Each tick: output = previous_output + 0.1, clamped to [0, 1]
    nomos::rt::modulator_engine eng;
    eng.start("acc", std::make_unique<nomos::rt::graph_modulator>(
        parse("[:clamp [:add [:mod-out :acc :cv] 0.1] 0.0 1.0]"), &eng));

    float prev = -1.0f;
    for (int i = 0; i < 10; ++i) {
        float cv = 0.0f;
        eng.tick(0.0, 100.0f, [&](const std::string&,
                                   const nomos::rt::modulator_output& out) {
            cv = out.cv;
        });
        if (prev >= 0.0f)
            REQUIRE(cv == Catch::Approx(prev + 0.1f).margin(1e-4f));
        prev = cv;
    }
}

TEST_CASE("graph_modulator: tick order — later-inserted mod sees previous-tick value",
          "[graph_modulator]") {
    nomos::rt::modulator_engine eng;

    // sink started before src → ticks before src; reads src's previous-tick output
    eng.start("sink", std::make_unique<nomos::rt::graph_modulator>(
        parse("[:mod-out :src :cv]"), &eng));
    eng.start("src",  std::make_unique<nomos::rt::graph_modulator>(parse("0.99"), &eng));

    // Tick 1: src not yet in last_outputs when sink ticks → sink sees 0.0
    std::map<std::string, float> tick1;
    eng.tick(0.0, 100.0f, [&](const std::string& id, const nomos::rt::modulator_output& out) {
        tick1[id] = out.cv;
    });
    REQUIRE(tick1["sink"] == Catch::Approx(0.0f));  // src not recorded yet when sink ticked

    // Tick 2: src was recorded in tick 1 → sink sees 0.99
    std::map<std::string, float> tick2;
    eng.tick(0.0, 100.0f, [&](const std::string& id, const nomos::rt::modulator_output& out) {
        tick2[id] = out.cv;
    });
    REQUIRE(tick2["sink"] == Catch::Approx(0.99f));
}

// ---------------------------------------------------------------------------
// Error cases
// ---------------------------------------------------------------------------

TEST_CASE("graph_modulator: unknown op produces silent output", "[graph_modulator]") {
    // parse_node returns -1 for unknown op → out_cv = -1 → cv stays 0
    auto r = edn::parse(std::string_view{"[:not-a-real-op 1.0]"});
    REQUIRE(r.has_value());
    nomos::rt::graph_modulator m{nomos::rt::parse_control_graph(*r)};
    REQUIRE(m.tick(0.0, 100.0f).cv == Catch::Approx(0.0f));
}

TEST_CASE("graph_modulator: empty graph produces silent output", "[graph_modulator]") {
    nomos::rt::control_graph g;  // no nodes, out_cv = -1
    nomos::rt::graph_modulator m{std::move(g)};
    REQUIRE_NOTHROW(m.tick(0.0, 100.0f));
    REQUIRE(m.tick(0.0, 100.0f).cv == Catch::Approx(0.0f));
}
