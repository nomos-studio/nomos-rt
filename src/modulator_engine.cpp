// SPDX-License-Identifier: LGPL-2.1-or-later
#include <nomos/rt/modulator_engine.hpp>

#include <algorithm>

namespace nomos::rt {

void modulator_engine::start(std::string id, std::unique_ptr<abstract_modulator> mod) {
    std::lock_guard lock{write_mu_};
    auto* old = table_.unsafe_get();
    auto  next = std::make_unique<table_t>(old ? *old : table_t{});
    if (next->mods.find(id) == next->mods.end())
        next->tick_order.push_back(id);  // new id: append; replacement: keep position
    next->mods[id] = std::move(mod);
    table_.store(std::move(next));
}

void modulator_engine::stop(std::string_view id) {
    std::lock_guard lock{write_mu_};
    auto* old = table_.unsafe_get();
    if (!old) return;
    auto  next = std::make_unique<table_t>(*old);
    next->mods.erase(std::string{id});
    auto& order = next->tick_order;
    order.erase(std::remove(order.begin(), order.end(), std::string{id}), order.end());
    table_.store(std::move(next));
}

void modulator_engine::update_param(std::string_view id, std::string_view key, float value) {
    std::lock_guard lock{write_mu_};
    auto* t = table_.unsafe_get();
    if (!t) return;
    auto it = t->mods.find(std::string{id});
    if (it != t->mods.end())
        it->second->update(key, value);
}

const modulator_output* modulator_engine::last_output(std::string_view id) const noexcept {
    auto it = last_outputs_.find(std::string{id});
    return it != last_outputs_.end() ? &it->second : nullptr;
}

std::size_t modulator_engine::size() const {
    std::lock_guard lock{write_mu_};
    auto* t = table_.unsafe_get();
    return t ? t->mods.size() : 0;
}

} // namespace nomos::rt
