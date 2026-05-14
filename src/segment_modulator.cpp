// SPDX-License-Identifier: LGPL-2.1-or-later
#include "segment_modulator.hpp"

#include "stages/segment_generator.h"
#include "stmlib/utils/gate_flags.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>

namespace nomos::rt {

segment_modulator::segment_modulator(const std::vector<segment_def>& segments)
    : defs_(segments) {
    gen_.Init();
    reconfigure();
}

float segment_modulator::tick(double /*beat*/, float /*tick_rate_hz*/) {
    // Process one virtual sample; gate=0 for free-running.
    gen_.Process(&gate_, &out_, 1);
    return std::clamp(out_.value, 0.0f, 1.0f) * depth_;
}

void segment_modulator::update(std::string_view key, float value) {
    if (key == "depth") {
        depth_ = std::clamp(value, 0.0f, 1.0f);
        return;
    }

    // "segment_N_primary" / "segment_N_secondary"
    // Parse N from the key and forward to the generator.
    int idx = -1;
    bool is_primary = false;

    if (key.size() > 9 && key.substr(0, 8) == "segment_") {
        const auto rest = key.substr(8);
        const auto sep  = rest.find('_');
        if (sep != std::string_view::npos) {
            // Parse index
            idx = 0;
            for (char c : rest.substr(0, sep)) {
                if (c < '0' || c > '9') { idx = -1; break; }
                idx = idx * 10 + (c - '0');
            }
            const auto field = rest.substr(sep + 1);
            if (field == "primary")   is_primary = true;
            else if (field == "secondary") is_primary = false;
            else idx = -1;
        }
    }

    if (idx >= 0 && idx < static_cast<int>(defs_.size())) {
        value = std::clamp(value, 0.0f, 1.0f);
        if (is_primary)
            defs_[static_cast<std::size_t>(idx)].primary = value;
        else
            defs_[static_cast<std::size_t>(idx)].secondary = value;
        gen_.set_segment_parameters(idx,
            defs_[static_cast<std::size_t>(idx)].primary,
            defs_[static_cast<std::size_t>(idx)].secondary);
    }
}

void segment_modulator::reconfigure() {
    const int n = static_cast<int>(
        std::min(defs_.size(), static_cast<std::size_t>(stages::kMaxNumSegments)));

    // Build configuration array on the stack.
    stages::segment::Configuration cfg[stages::kMaxNumSegments];
    for (int i = 0; i < n; ++i) {
        cfg[i].type = defs_[static_cast<std::size_t>(i)].type;
        cfg[i].loop = defs_[static_cast<std::size_t>(i)].loop;
    }

    // has_trigger=false: free-running; no external gate handling yet.
    gen_.Configure(false, cfg, n);

    // Apply per-segment parameters.
    for (int i = 0; i < n; ++i) {
        gen_.set_segment_parameters(
            i,
            defs_[static_cast<std::size_t>(i)].primary,
            defs_[static_cast<std::size_t>(i)].secondary);
    }
}

} // namespace nomos::rt
