// SPDX-License-Identifier: LGPL-2.1-or-later
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "faust_modulator.hpp"
#include "nomos/rt/dsp_block.hpp"

#include <string>
#include <unordered_map>

using nomos::rt::dsp_block;
using nomos::rt::faust_modulator;

namespace {

// Stub dsp_block: remembers the last beat param, returns configurable outputs.
class stub_dsp final : public dsp_block {
public:
    int   num_outs      = 4;
    float out_values[4] = {0.3f, 0.6f, 0.8f, 0.2f};
    float last_beat     = -1.0f;
    float last_rate     = -1.0f;
    int   init_count    = 0;
    std::unordered_map<std::string, float> params;

    void init(float sr) override { last_rate = sr; ++init_count; }
    int  num_outputs() const override { return num_outs; }

    void set_param(std::string_view key, float value) override {
        params[std::string(key)] = value;
        if (key == "beat") last_beat = value;
    }

    void process(float** outputs) override {
        for (int i = 0; i < num_outs; ++i)
            *outputs[i] = out_values[i];
    }
};

constexpr double kBeat     = 1.375;
constexpr float  kRate     = 375.0f;
constexpr float  kBeatPhase = 0.375f;   // 1.375 - floor(1.375)

} // namespace

TEST_CASE("faust_modulator: default construction does not crash", "[faust_mod]") {
    REQUIRE_NOTHROW(faust_modulator{std::make_unique<stub_dsp>()});
}

TEST_CASE("faust_modulator: init called on first tick", "[faust_mod]") {
    auto* raw = new stub_dsp;
    faust_modulator m{std::unique_ptr<dsp_block>(raw)};
    m.tick(kBeat, kRate);
    REQUIRE(raw->init_count == 1);
    REQUIRE(raw->last_rate  == Catch::Approx(kRate));
}

TEST_CASE("faust_modulator: beat phase delivered as 'beat' param", "[faust_mod]") {
    auto* raw = new stub_dsp;
    faust_modulator m{std::unique_ptr<dsp_block>(raw)};
    m.tick(kBeat, kRate);
    REQUIRE(raw->last_beat == Catch::Approx(kBeatPhase).margin(1e-5f));
}

TEST_CASE("faust_modulator: output channel 0 maps to cv", "[faust_mod]") {
    auto* raw = new stub_dsp;
    raw->out_values[0] = 0.42f;
    faust_modulator m{std::unique_ptr<dsp_block>(raw)};
    const auto out = m.tick(kBeat, kRate);
    REQUIRE(out.cv       == Catch::Approx(0.42f));
    REQUIRE(out.outputs[0] == Catch::Approx(0.42f));
}

TEST_CASE("faust_modulator: output channel 1 maps to aux", "[faust_mod]") {
    auto* raw = new stub_dsp;
    raw->out_values[1] = 0.55f;
    faust_modulator m{std::unique_ptr<dsp_block>(raw)};
    const auto out = m.tick(kBeat, kRate);
    REQUIRE(out.aux        == Catch::Approx(0.55f));
    REQUIRE(out.outputs[1] == Catch::Approx(0.55f));
}

TEST_CASE("faust_modulator: channel 2 > 0.5 sets gate", "[faust_mod]") {
    auto* raw = new stub_dsp;
    raw->out_values[2] = 0.8f;
    faust_modulator m{std::unique_ptr<dsp_block>(raw)};
    REQUIRE(m.tick(kBeat, kRate).gate == true);
    raw->out_values[2] = 0.2f;
    REQUIRE(m.tick(kBeat, kRate).gate == false);
}

TEST_CASE("faust_modulator: channel 3 > 0.5 sets gate2", "[faust_mod]") {
    auto* raw = new stub_dsp;
    raw->out_values[3] = 0.9f;
    faust_modulator m{std::unique_ptr<dsp_block>(raw)};
    REQUIRE(m.tick(kBeat, kRate).gate2 == true);
    raw->out_values[3] = 0.1f;
    REQUIRE(m.tick(kBeat, kRate).gate2 == false);
}

TEST_CASE("faust_modulator: single-output patch leaves aux/gate zero", "[faust_mod]") {
    auto* raw = new stub_dsp;
    raw->num_outs = 1;
    raw->out_values[0] = 0.7f;
    faust_modulator m{std::unique_ptr<dsp_block>(raw)};
    const auto out = m.tick(kBeat, kRate);
    REQUIRE(out.cv      == Catch::Approx(0.7f));
    REQUIRE(out.aux     == Catch::Approx(0.0f));
    REQUIRE(out.gate    == false);
    REQUIRE(out.gate2   == false);
}

TEST_CASE("faust_modulator: state bitmask reflects outputs > 0.5", "[faust_mod]") {
    auto* raw = new stub_dsp;
    //  ch0=0.3 (below), ch1=0.6 (above), ch2=0.8 (above), ch3=0.2 (below)
    raw->out_values[0] = 0.3f;
    raw->out_values[1] = 0.6f;
    raw->out_values[2] = 0.8f;
    raw->out_values[3] = 0.2f;
    faust_modulator m{std::unique_ptr<dsp_block>(raw)};
    const auto out = m.tick(kBeat, kRate);
    REQUIRE((out.state & 0x1u) == 0u);   // ch0 below
    REQUIRE((out.state & 0x2u) != 0u);   // ch1 above
    REQUIRE((out.state & 0x4u) != 0u);   // ch2 above
    REQUIRE((out.state & 0x8u) == 0u);   // ch3 below
}

TEST_CASE("faust_modulator: update forwards to dsp set_param", "[faust_mod]") {
    auto* raw = new stub_dsp;
    faust_modulator m{std::unique_ptr<dsp_block>(raw)};
    m.update("cutoff", 0.5f);
    REQUIRE(raw->params.at("cutoff") == Catch::Approx(0.5f));
}

TEST_CASE("faust_modulator: re-init when rate changes significantly", "[faust_mod]") {
    auto* raw = new stub_dsp;
    faust_modulator m{std::unique_ptr<dsp_block>(raw)};
    m.tick(0.0, 375.0f);
    REQUIRE(raw->init_count == 1);
    m.tick(0.0, 375.5f);   // < 1 Hz delta — no re-init
    REQUIRE(raw->init_count == 1);
    m.tick(0.0, 500.0f);   // > 1 Hz delta — re-init
    REQUIRE(raw->init_count == 2);
}

TEST_CASE("faust_modulator: unknown update key is a no-op", "[faust_mod]") {
    auto* raw = new stub_dsp;
    faust_modulator m{std::unique_ptr<dsp_block>(raw)};
    REQUIRE_NOTHROW(m.update("bogus", 1.0f));
    REQUIRE_NOTHROW(m.tick(kBeat, kRate));
}
