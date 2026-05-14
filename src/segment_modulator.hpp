// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "abstract_modulator.hpp"

#include "stages/segment_generator.h"

#include <string_view>
#include <vector>

namespace nomos::rt {

// Stages-inspired multi-segment function generator / LFO.
//
// Wraps a single stages::SegmentGenerator which supports up to
// kMaxNumSegments=36 segments — equivalent to chaining 6 hardware Stages
// modules.  No instance coordination is needed; the class handles the full
// chain natively.
//
// Each segment is described by:
//   type      — :ramp | :step | :hold | :alt
//   primary   — Time (ramp) | Level (hold/step)   range [0, 1]
//   secondary — Shape (ramp/step) | Time (hold)   range [0, 1]
//   loop      — if true on the last segment, the voice free-runs
//
// update() keys:
//   "segment_N_primary"   — update primary of segment N (0-indexed)
//   "segment_N_secondary" — update secondary of segment N
//   "depth"               — output scale [0..1] (default 1.0)
//
// Triggering is not yet implemented; set loop=true on the last segment for
// free-running LFO behaviour.
class segment_modulator final : public abstract_modulator {
public:
    struct segment_def {
        stages::segment::Type type{stages::segment::TYPE_RAMP};
        float                 primary{0.5f};    // [0, 1]
        float                 secondary{0.5f};  // [0, 1]
        bool                  loop{false};
    };

    explicit segment_modulator(const std::vector<segment_def>& segments);

    float tick(double beat, float tick_rate_hz) override;
    void  update(std::string_view key, float value) override;

private:
    void reconfigure();

    stages::SegmentGenerator gen_;
    std::vector<segment_def> defs_;
    float                    depth_{1.0f};

    // Single-sample output buffer.
    stages::SegmentGenerator::Output out_{};
    stmlib::GateFlags                gate_{0};
};

} // namespace nomos::rt
