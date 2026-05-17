// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "abstract_modulator.hpp"
#include <nomos/rt/rcu.hpp>

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace nomos::rt {

// RT modulator engine — runs autonomous modulators at event-loop block rate.
//
// Threading model:
//   Event thread — calls tick(), which iterates the active modulator table under
//   a lock-free RCU read guard.  tick() never blocks for control operations.
//
//   Control thread — calls start(), stop(), update_param() under a write mutex.
//   start()/stop() copy the table (copy-on-write) before installing the new
//   version via rcu_managed::store().  update_param() locates the modulator
//   under the write mutex and calls update(); there is a benign relaxed race
//   between update() and a concurrent tick() on the same modulator's float
//   members — negligible at control rate.
//
// tick() is a function template to avoid std::function overhead; the callback
// type is deduced from the caller.  Signature expected:
//   void cb(const std::string& id, const modulator_output& out)
class modulator_engine {
public:
    // Register a new modulator under the given id.  Replaces any existing
    // modulator with the same id.
    void start(std::string id, std::unique_ptr<abstract_modulator> mod);

    // Remove and destroy the modulator with the given id.  No-op if absent.
    void stop(std::string_view id);

    // Forward a parameter update to the named modulator.  No-op if absent.
    void update_param(std::string_view id, std::string_view key, float value);

    // Tick all active modulators and invoke cb for each output.
    // Lock-free on the event thread; cb is invoked inside the RCU read-side CS.
    template <typename Fn>
    void tick(double beat, float tick_rate_hz, Fn&& cb) {
        auto guard = table_.read();
        if (!guard) return;
        for (auto& [id, mod] : guard->mods)
            cb(id, mod->tick(beat, tick_rate_hz));
    }

    // Number of active modulators (acquires write mutex; not for use on event thread).
    std::size_t size() const;

private:
    using mod_map = std::unordered_map<std::string, std::shared_ptr<abstract_modulator>>;

    struct table_t {
        mod_map mods;
    };

    mutable std::mutex         write_mu_;
    rcu_managed<table_t>       table_{std::make_unique<table_t>()};
};

} // namespace nomos::rt
