// SPDX-License-Identifier: LGPL-2.1-or-later
#include "segment_modulator.hpp"

#include <algorithm>
#include <cmath>
#include <span>

namespace nomos::rt {

segment_modulator::segment_modulator(std::span<const segment_def> segments) {
    n_defs_ = std::clamp(static_cast<int>(segments.size()), 1, kMaxSegments);
    for (int i = 0; i < n_defs_; ++i)
        defs_[static_cast<std::size_t>(i)] = segments[static_cast<std::size_t>(i)];
}

modulator_output segment_modulator::tick(double /*beat*/, float tick_rate_hz) {
    if (tick_rate_hz <= 0.0f)
        return {.cv = cur_output_ * depth_};

    const int n = n_defs_;

    phase_ += rate_ / tick_rate_hz;
    if (phase_ >= 1.0f) {
        phase_ -= 1.0f;
        ++cycle_count_;
    }

    const float seg_phase = phase_ * static_cast<float>(n);
    const int   new_seg   = static_cast<int>(seg_phase) % n;
    const float local_p   = seg_phase - std::floor(seg_phase);

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
    return {.cv = cur_output_ * depth_};
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
    if (idx < 0 || idx >= n_defs_)
        return;

    const auto  field = rest.substr(sep + 1);
    const float v     = std::clamp(value, 0.0f, 1.0f);
    auto& def = defs_[static_cast<std::size_t>(idx)];
    if      (field == "primary")   def.primary   = v;
    else if (field == "secondary") def.secondary = v;
}

} // namespace nomos::rt
