// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "abstract_modulator.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace nomos::rt {

using modulator_output_fn = std::function<void(const std::string& id, float value)>;

// RT modulator engine — runs autonomous modulators at event-loop block rate.
//
// Threading model:
//   Event thread — calls tick(), which runs all active modulators.
//   Control thread — calls start(), stop(), update_param() via IPC dispatch.
// All methods are mutex-guarded so event and control threads share the table
// safely; contention is negligible because control operations are infrequent.
class modulator_engine {
public:
    // Register a new modulator under the given id.  Replaces any existing
    // modulator with the same id (old one is destroyed before the new one runs).
    void start(std::string id, std::unique_ptr<abstract_modulator> mod);

    // Remove and destroy the modulator with the given id.  No-op if absent.
    void stop(std::string_view id);

    // Forward a parameter update to the named modulator.  No-op if absent.
    void update_param(std::string_view id, std::string_view key, float value);

    // Tick all active modulators.
    // out: called once per modulator with (id, normalised_value).
    // beat and tick_rate_hz are forwarded to each modulator unchanged.
    void tick(double beat, float tick_rate_hz, const modulator_output_fn& out);

    // Number of active modulators.
    std::size_t size() const;

private:
    mutable std::mutex                                            mu_;
    std::unordered_map<std::string, std::unique_ptr<abstract_modulator>> mods_;
};

} // namespace nomos::rt
