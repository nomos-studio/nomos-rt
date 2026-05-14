// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <nomos/rt/time_identity.hpp>

#include <edn/value.hpp>

namespace nomos::rt {

// A parameter change queued by the control thread for the audio thread.
// `path` is an EDN vector of keywords, e.g. [:fm-voice/voice-1 :carrier].
// `value` is the new parameter value.
// `time` carries the (current, pending) tuple from the originating MSG-PARAM-SET.
struct param_event {
    edn::value    path;
    edn::value    value;
    time_identity time;
};

} // namespace nomos::rt
