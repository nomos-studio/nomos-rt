// SPDX-License-Identifier: GPL-2.0-or-later
#include "link_peer.hpp"

namespace nomos::rt {

link_peer::link_peer(double initial_bpm) : link_(initial_bpm) {
}

void link_peer::enable(bool on) {
    link_.enable(on);
}

bool link_peer::enabled() const noexcept {
    return link_.isEnabled();
}

std::size_t link_peer::peer_count() const noexcept {
    return link_.numPeers();
}

std::chrono::microseconds link_peer::now() const noexcept {
    return link_.clock().micros();
}

double link_peer::beat_at_time(std::chrono::microseconds host_time, double quantum) const {
    const auto state = link_.captureAppSessionState();
    return state.beatAtTime(host_time, quantum);
}

double link_peer::tempo() const {
    const auto state = link_.captureAppSessionState();
    return state.tempo();
}

void link_peer::set_tempo(double bpm, std::chrono::microseconds when) {
    auto state = link_.captureAppSessionState();
    state.setTempo(bpm, when);
    link_.commitAppSessionState(state);
}

} // namespace nomos::rt
