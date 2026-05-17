// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <nomos/rt/abstract_modulator.hpp>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace nomos::rt {

// A no-arg factory that creates a new modulator instance.
// Parameters are applied after construction via abstract_modulator::update().
// Use a capturing lambda to bake in construction-time options:
//
//   registry.register_type("shift-register-turing", []() {
//       return std::make_unique<shift_register_modulator>(
//           shift_register_modulator::mode::turing, 16, 3);
//   });
using modulator_factory = std::function<std::unique_ptr<abstract_modulator>()>;

// Maps type-name strings to modulator factories.
//
// Plugged into rt_control_thread::config::ext_registry to handle MSG-MODULATOR-START
// messages whose :type field is not one of the built-in types.  After the factory
// constructs the modulator, the control thread applies all float-typed EDN parameters
// via update(), so the update() interface is the only configuration surface needed.
//
// Built-in types (slope, segment, slew, shift-register, fractal, stochastic) are
// handled by rt_control_thread directly and do not need to be registered here.
class modulator_registry {
public:
    // Register a factory under name.  Replaces any existing factory for that name.
    void register_type(std::string name, modulator_factory fn) {
        factories_[std::move(name)] = std::move(fn);
    }

    // Create a new modulator instance.  Returns nullptr if name is not registered.
    [[nodiscard]] std::unique_ptr<abstract_modulator> make(std::string_view name) const {
        const auto it = factories_.find(std::string{name});
        return (it != factories_.end()) ? it->second() : nullptr;
    }

    bool contains(std::string_view name) const {
        return factories_.count(std::string{name}) > 0;
    }

private:
    std::unordered_map<std::string, modulator_factory> factories_;
};

} // namespace nomos::rt
