// SPDX-License-Identifier: LGPL-2.1-or-later
#include "modulator_engine.hpp"

namespace nomos::rt {

void modulator_engine::start(std::string id, std::unique_ptr<abstract_modulator> mod) {
    std::lock_guard lock{mu_};
    mods_[std::move(id)] = std::move(mod);
}

void modulator_engine::stop(std::string_view id) {
    std::lock_guard lock{mu_};
    mods_.erase(std::string{id});
}

void modulator_engine::update_param(std::string_view id, std::string_view key, float value) {
    std::lock_guard lock{mu_};
    if (auto it = mods_.find(std::string{id}); it != mods_.end())
        it->second->update(key, value);
}

void modulator_engine::tick(double beat, float tick_rate_hz, const modulator_output_fn& out) {
    std::lock_guard lock{mu_};
    for (auto& [id, mod] : mods_) {
        const float v = mod->tick(beat, tick_rate_hz);
        if (out)
            out(id, v);
    }
}

std::size_t modulator_engine::size() const {
    std::lock_guard lock{mu_};
    return mods_.size();
}

} // namespace nomos::rt
