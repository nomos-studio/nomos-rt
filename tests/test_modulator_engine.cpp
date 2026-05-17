// SPDX-License-Identifier: LGPL-2.1-or-later
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "modulator_engine.hpp"

#include <map>
#include <memory>
#include <string>

namespace {

// Minimal stub modulator: returns a fixed value set via update("value", v).
class stub_modulator final : public nomos::rt::abstract_modulator {
public:
    nomos::rt::modulator_output tick(double /*beat*/, float /*tick_rate_hz*/) override {
        return {.cv = value_};
    }
    void update(std::string_view key, float v) override {
        if (key == "value")
            value_ = v;
    }
    float value_{0.5f};
};

} // namespace

TEST_CASE("modulator_engine: empty on construction", "[modulator_engine]") {
    nomos::rt::modulator_engine eng;
    REQUIRE(eng.size() == 0);
}

TEST_CASE("modulator_engine: start adds a modulator", "[modulator_engine]") {
    nomos::rt::modulator_engine eng;
    eng.start("lfo1", std::make_unique<stub_modulator>());
    REQUIRE(eng.size() == 1);
}

TEST_CASE("modulator_engine: stop removes a modulator", "[modulator_engine]") {
    nomos::rt::modulator_engine eng;
    eng.start("lfo1", std::make_unique<stub_modulator>());
    eng.start("lfo2", std::make_unique<stub_modulator>());
    REQUIRE(eng.size() == 2);

    eng.stop("lfo1");
    REQUIRE(eng.size() == 1);

    eng.stop("lfo2");
    REQUIRE(eng.size() == 0);
}

TEST_CASE("modulator_engine: stop on unknown id is a no-op", "[modulator_engine]") {
    nomos::rt::modulator_engine eng;
    eng.start("lfo1", std::make_unique<stub_modulator>());
    eng.stop("nonexistent");
    REQUIRE(eng.size() == 1);
}

TEST_CASE("modulator_engine: start replaces existing id", "[modulator_engine]") {
    nomos::rt::modulator_engine eng;
    eng.start("lfo1", std::make_unique<stub_modulator>());
    eng.start("lfo1", std::make_unique<stub_modulator>());
    REQUIRE(eng.size() == 1);
}

TEST_CASE("modulator_engine: tick dispatches output with correct id", "[modulator_engine]") {
    nomos::rt::modulator_engine eng;
    auto* stub = new stub_modulator();
    stub->value_ = 0.75f;
    eng.start("mod-a", std::unique_ptr<stub_modulator>(stub));

    std::map<std::string, float> received;
    eng.tick(0.0, 100.0f, [&](const std::string& id, const nomos::rt::modulator_output& out) {
        received[id] = out.cv;
    });

    REQUIRE(received.count("mod-a") == 1);
    REQUIRE(received["mod-a"] == Catch::Approx(0.75f));
}

TEST_CASE("modulator_engine: tick dispatches all active modulators", "[modulator_engine]") {
    nomos::rt::modulator_engine eng;
    auto* a = new stub_modulator(); a->value_ = 0.1f;
    auto* b = new stub_modulator(); b->value_ = 0.9f;
    eng.start("a", std::unique_ptr<stub_modulator>(a));
    eng.start("b", std::unique_ptr<stub_modulator>(b));

    std::map<std::string, float> received;
    eng.tick(4.0, 100.0f, [&](const std::string& id, const nomos::rt::modulator_output& out) {
        received[id] = out.cv;
    });

    REQUIRE(received.size() == 2);
    REQUIRE(received["a"] == Catch::Approx(0.1f));
    REQUIRE(received["b"] == Catch::Approx(0.9f));
}

TEST_CASE("modulator_engine: tick with no-op callback does not crash", "[modulator_engine]") {
    nomos::rt::modulator_engine eng;
    eng.start("lfo1", std::make_unique<stub_modulator>());
    REQUIRE_NOTHROW(eng.tick(0.0, 100.0f,
        [](const std::string&, const nomos::rt::modulator_output&) {}));
}

TEST_CASE("modulator_engine: update_param routes to correct modulator", "[modulator_engine]") {
    nomos::rt::modulator_engine eng;
    auto* stub = new stub_modulator();
    stub->value_ = 0.0f;
    eng.start("m1", std::unique_ptr<stub_modulator>(stub));

    eng.update_param("m1", "value", 0.42f);

    float result = 0.0f;
    eng.tick(0.0, 100.0f, [&](const std::string& /*id*/, const nomos::rt::modulator_output& out) {
        result = out.cv;
    });
    REQUIRE(result == Catch::Approx(0.42f));
}

TEST_CASE("modulator_engine: update_param on unknown id is a no-op", "[modulator_engine]") {
    nomos::rt::modulator_engine eng;
    eng.start("m1", std::make_unique<stub_modulator>());
    REQUIRE_NOTHROW(eng.update_param("ghost", "value", 1.0f));
    REQUIRE(eng.size() == 1);
}

TEST_CASE("modulator_engine: stopped modulator no longer ticked", "[modulator_engine]") {
    nomos::rt::modulator_engine eng;
    eng.start("lfo1", std::make_unique<stub_modulator>());
    eng.stop("lfo1");

    int call_count = 0;
    eng.tick(0.0, 100.0f, [&](const std::string& /*id*/, const nomos::rt::modulator_output&) {
        ++call_count;
    });
    REQUIRE(call_count == 0);
}
