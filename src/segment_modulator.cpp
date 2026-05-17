// SPDX-License-Identifier: LGPL-2.1-or-later
#include "segment_modulator.hpp"

#include <algorithm>
#include <cmath>

namespace nomos::rt {

segment_modulator::segment_modulator(const std::vector<segment_def>& segments)
    : defs_(segments.empty() ? std::vector<segment_def>{{}} : segments)
{
    defs_.resize(std::min(static_cast<int>(defs_.size()), kMaxSegments));
}

float segment_modulator::tick(double /*beat*/, float tick_rate_hz) {
    if (tick_rate_hz <= 0.0f)
        return cur_output_ * depth_;

    const int n = static_cast<int>(defs_.size());

    // Advance phase; one full cycle = 1/rate_ seconds.
    phase_ += rate_ / tick_rate_hz;
    if (phase_ >= 1.0f) {
        phase_ -= 1.0f;
        ++cycle_count_;
    }

    // Current segment index and local phase within it.
    const float seg_phase = phase_ * static_cast<float>(n);
    const int   new_seg   = static_cast<int>(seg_phase) % n;
    const float local_p   = seg_phase - std::floor(seg_phase);

    // On segment transition, capture the start value for ramp interpolation.
    if (new_seg != cur_seg_) {
        seg_start_val_ = cur_output_;
        cur_seg_       = new_seg;
    }

    const auto& seg = defs_[static_cast<std::size_t>(cur_seg_)];
    float output;
    switch (seg.kind) {
        case type::ramp:
            output = seg_start_val_ + local_p * (seg.primary - seg_start_val_);
            break;
        case type::step:
            output = seg.primary;
            break;
        case type::hold:
            output = seg_start_val_;
            break;
        case type::alt:
            output = (cycle_count_ % 2 == 0) ? seg.primary : seg.secondary;
            break;
    }

    cur_output_ = std::clamp(output, 0.0f, 1.0f);
    return cur_output_ * depth_;
}

void segment_modulator::update(std::string_view key, float value) {
    if (key == "depth") {
        depth_ = std::clamp(value, 0.0f, 1.0f);
        return;
    }
    if (key == "rate") {
        rate_ = std::clamp(value, 0.001f, 100.0f);
        return;
    }

    // "segment_N_primary" / "segment_N_secondary"
    if (key.size() <= 8 || key.substr(0, 8) != "segment_")
        return;

    const auto rest = key.substr(8);
    const auto sep  = rest.find('_');
    if (sep == std::string_view::npos)
        return;

    int idx = 0;
    for (char c : rest.substr(0, sep)) {
        if (c < '0' || c > '9') return;
        idx = idx * 10 + (c - '0');
    }
    if (idx < 0 || idx >= static_cast<int>(defs_.size()))
        return;

    const auto field = rest.substr(sep + 1);
    const float v    = std::clamp(value, 0.0f, 1.0f);
    auto& def = defs_[static_cast<std::size_t>(idx)];
    if      (field == "primary")   def.primary   = v;
    else if (field == "secondary") def.secondary = v;
}

} // namespace nomos::rt
