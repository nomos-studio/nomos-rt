// SPDX-License-Identifier: LGPL-2.1-or-later
#include <nomos/rt/session.hpp>

namespace nomos::rt {

session::session(std::string_view db_path) : log_(std::make_unique<txlog::log>(db_path)) {
}

std::optional<session> session::open(std::string_view db_path) {
    try {
        return session{db_path};
    } catch (...) {
        return std::nullopt;
    }
}

void session::close() {
    log_.reset();
}

bool session::is_open() const noexcept {
    return log_ != nullptr;
}

void session::register_source(source_info info) {
    if (!log_)
        return;
    log_->register_source(info.id, info.name, info.description);
}

void session::emit(const txlog::entry& e) {
    if (!log_)
        return;
    log_->emit(e);
}

txlog::log& session::log() {
    return *log_;
}

} // namespace nomos::rt
