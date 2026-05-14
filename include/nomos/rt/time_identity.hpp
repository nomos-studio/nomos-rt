// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <optional>

#include <edn/value.hpp>

namespace nomos::rt {

struct timeline {
    double bpm{120.0};
    double beat{0.0};
};

// Mirrors the Clojure shape from cljseq.link:
//   {:timeline <tl> :policy :bar-quantize :apply-at 36.0}
// `:apply_at` is the resolved beat; authoritative for firing.
// `:policy` is metadata for tx_log and tooling — not a re-resolve instruction.
struct pending_transition {
    timeline     tl;
    edn::keyword policy;   // :bar-quantize | :snap | :smooth
    double       apply_at; // resolved beat — fire when current_beat >= apply_at
};

struct time_identity {
    timeline                          current;
    std::optional<pending_transition> pending;

    bool transition_ready(double current_beat) const noexcept {
        return pending.has_value() && current_beat >= pending->apply_at;
    }

    // Promotes pending → current if apply_at has been reached.  Returns true if promoted.
    // Call from the audio thread when the block's beat position crosses apply_at.
    bool apply_if_ready(double current_beat) noexcept {
        if (!transition_ready(current_beat))
            return false;
        current = pending->tl;
        pending.reset();
        return true;
    }
};

} // namespace nomos::rt
