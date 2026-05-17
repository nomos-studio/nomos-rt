// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <nomos/rt/abstract_modulator.hpp>

#include <array>
#include <span>
#include <string_view>

namespace nomos::rt {

// Multi-segment function generator / LFO.
//
// N segments (up to 36) run in sequence, each occupying an equal share of the
// total period.  Output is unipolar [0, 1] before depth scaling.  The generator
// always free-runs (loops continuously).
//
// Segment types:
//   ramp — linearly interpolates from the previous segment's end value to primary
//   step — immediate jump to primary, held for the segment duration
//   hold — holds the value at the start of this segment (no change)
//   alt  — alternates between primary and secondary on each successive cycle
//
// Parameters (via update()):
//   "rate"                — full-cycle rate in Hz  [0.001, 100]  (default 1.0)
//   "segment_N_primary"   — primary value of segment N  [0, 1]
//   "segment_N_secondary" — secondary value of segment N  [0, 1]
//   "depth"               — output scale  [0, 1]  (default 1.0)
class segment_modulator final : public abstract_modulator {
public:
    enum class type { ramp, step, hold, alt };

    struct segment_def {
        type  kind{type::ramp};
        float primary{0.5f};
        float secondary{0.5f};
        bool  loop{false};  // reserved — generator always loops
    };

    explicit segment_modulator(std::span<const segment_def> segments);

    modulator_output tick(double beat, float tick_rate_hz) override;
    void             update(std::string_view key, float value) override;

private:
    static constexpr int kMaxSegments = 36;

    std::array<segment_def, kMaxSegments> defs_{};
    int   n_defs_{1};
    float depth_{1.0f};
    float rate_{1.0f};

    float phase_{0.0f};
    int   cur_seg_{0};
    int   cycle_count_{0};
    float seg_start_val_{0.0f};
    float cur_output_{0.0f};
};

} // namespace nomos::rt
