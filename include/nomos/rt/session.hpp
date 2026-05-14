// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <edn/value.hpp>
#include <txlog/txlog.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace nomos::rt {

struct source_info {
    edn::keyword id;
    std::string  name;
    std::string  description;
};

// Owns a txlog::log instance for the duration of a session.
// open() and close() map to MSG-SESSION-OPEN / MSG-SESSION-CLOSE.
// register_source() maps to MSG-REGISTER-SOURCE.
//
// Not thread-safe by itself — all calls must come from the control thread.
// The txlog::log::emit() method is internally mutex-protected and safe to
// call from the audio thread via a forwarding helper.
class session {
  public:
    // Opens (or creates) the txlog database at db_path.
    // Returns std::nullopt if the database cannot be opened.
    static std::optional<session> open(std::string_view db_path);

    void close();
    bool is_open() const noexcept;

    void register_source(source_info info);

    // Forward an entry to txlog::log::emit().  Safe to call from any thread.
    void emit(const txlog::entry& e);

    txlog::log& log();

  private:
    explicit session(std::string_view db_path);

    std::unique_ptr<txlog::log> log_;
};

} // namespace nomos::rt
