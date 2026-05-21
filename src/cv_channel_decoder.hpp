// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <nomos/rt/abstract_modulator.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace nomos::rt {

class modulator_engine;

// CV-to-gate-array decoder with velocity extraction.
//
// Divides the input span [-1, 1] into N equal bands. Each band fires a gate
// when the input falls within its active detection window. SPACE controls the
// detection bandwidth as a fraction of the band half-width:
//   space=0.0 → vanishingly narrow (rarely fires)
//   space=1.0 → active across the full band (clean partition, default)
//   space>1.0 → bands overlap; multiple channels can fire simultaneously
//
// N=1: degenerate Schmitt trigger. One band centred at 0; gate fires when the
// input is within ±space of centre. SPACE controls the hysteresis window.
//
// Velocity output: |dSPAN/dt| normalised to [0, 1]. A full-range sweep
// [-1, 1] in one second yields velocity=1.0.
//
// Clock mode (update("clocked", 1.0f)): channel gates latch only on clock
// edges. Drive via update("clock_tick", 1.0f) once per clock edge — the flag
// is consumed (one-shot) on the next tick.
//
// Cross-modulator span source: pass a non-empty source_id at construction to
// read span from engine->last_output(source_id). source_field selects which
// output field (cv / aux / gate) is treated as the span signal. If the engine
// is null or the source has not yet produced output, falls back to the "span"
// update() parameter.
//
// Outputs (modulator_output):
//   .cv    — velocity [0, 1]
//   .aux   — raw SPAN passthrough [-1, 1]
//   .gate  — any channel active (OR of all channel bits)
//   .state — channel bitmap: bit N set when channel N is active
//
// Parameters (via update()):
//   "span"       — input signal [-1, 1]           (default 0.0; ignored when
//                  source_id is set and source has output)
//   "space"      — band detection fraction [0, 2]  (default 1.0)
//   "clocked"    — >0.5 enables clock-latch mode   (default off)
//   "clock_tick" — >0.5 arms one clock edge (one-shot)
class cv_channel_decoder final : public abstract_modulator {
public:
    enum class source_field { cv, aux, gate };

    // channels  — number of detection bands [1, 8]
    // engine    — if non-null, used to read cross-modulator span source
    // source_id — if non-empty, reads engine->last_output(source_id) for span
    // field     — which output field of the source to use as span
    explicit cv_channel_decoder(int channels = 1,
                                const modulator_engine* engine  = nullptr,
                                std::string             source_id = {},
                                source_field            field   = source_field::cv);

    modulator_output tick(double beat, float tick_rate_hz) override;
    void             update(std::string_view key, float value) override;

    static constexpr int kMaxChannels = 8;

private:
    float read_span()                              const noexcept;
    bool  channel_active(int ch, float span)       const noexcept;

    int                     channels_;
    const modulator_engine* engine_;
    std::string             source_id_;
    source_field            source_field_;

    float    span_{0.0f};
    float    space_{1.0f};
    float    prev_span_{0.0f};
    bool     first_tick_{true};

    bool     clocked_{false};
    bool     clock_pending_{false};
    uint32_t latched_state_{0};
};

} // namespace nomos::rt
