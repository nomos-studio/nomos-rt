// SPDX-License-Identifier: LGPL-2.1-or-later
#include "modulator_engine.hpp"

namespace nomos::rt {

void modulator_engine::start(std::string id, std::unique_ptr<abstract_modulator> mod) {
    std::lock_guard lock{write_mu_};
    auto* old = table_.unsafe_get();
    auto  next = std::make_unique<table_t>(old ? *old : table_t{});
    next->mods[std::move(id)] = std::move(mod);
    table_.store(std::move(next));
}

void modulator_engine::stop(std::string_view id) {
    std::lock_guard lock{write_mu_};
    auto* old = table_.unsafe_get();
    if (!old) return;
    auto next = std::make_unique<table_t>(*old);
    next->mods.erase(std::string{id});
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

std::size_t modulator_engine::size() const {
    std::lock_guard lock{write_mu_};
    auto* t = table_.unsafe_get();
    return t ? t->mods.size() : 0;
}

} // namespace nomos::rt
