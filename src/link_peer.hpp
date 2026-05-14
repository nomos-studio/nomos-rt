// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <ableton/Link.hpp>

#include <chrono>
#include <cstddef>

namespace nomos::rt {

// Wraps ableton::Link.  Lives only in the executable (GPL scope) — not exposed
// via kairos-core headers.
//
// App-thread API only.  Audio-thread capture (captureAudioSessionState) is
// added when the audio callback is wired in.
class link_peer {
  public:
    explicit link_peer(double initial_bpm = 120.0);

    void        enable(bool on);
    bool        enabled() const noexcept;
    std::size_t peer_count() const noexcept;

    // Current host clock time in microseconds (same epoch as Link).
    std::chrono::microseconds now() const noexcept;

    // Beat position at host_time.  quantum = beats per bar (default 4).
    double beat_at_time(std::chrono::microseconds host_time, double quantum = 4.0) const;

    // Consensus tempo from the Link session.
    double tempo() const;

    // Request a tempo change effective at `when` (microseconds).
    void set_tempo(double bpm, std::chrono::microseconds when);

  private:
    ableton::Link link_;
};

} // namespace nomos::rt
