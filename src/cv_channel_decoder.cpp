// SPDX-License-Identifier: LGPL-2.1-or-later
#include "cv_channel_decoder.hpp"

#include <nomos/rt/modulator_engine.hpp>

#include <algorithm>
#include <cmath>

namespace nomos::rt {

cv_channel_decoder::cv_channel_decoder(int channels,
                                       const modulator_engine* engine,
                                       std::string source_id,
                                       source_field field)
    : channels_(std::clamp(channels, 1, kMaxChannels))
    , engine_(engine)
    , source_id_(std::move(source_id))
    , source_field_(field)
{}

float cv_channel_decoder::read_span() const noexcept {
    if (engine_ && !source_id_.empty()) {
        const auto* out = engine_->last_output(source_id_);
        if (out) {
            switch (source_field_) {
                case source_field::cv:   return out->cv;
                case source_field::aux:  return out->aux;
                case source_field::gate: return out->gate ? 1.0f : -1.0f;
            }
        }
    }
    return span_;
}

bool cv_channel_decoder::channel_active(int ch, float span) const noexcept {
    // Channel ch occupies the band [-1 + 2*ch/N, -1 + 2*(ch+1)/N].
    // Centre = -1 + (2*ch + 1) / N.
    // Active window = [ctr - space*hw, ctr + space*hw] where hw = 1/N.
    const float n   = static_cast<float>(channels_);
    const float hw  = 1.0f / n;
    const float ctr = -1.0f + (2.0f * static_cast<float>(ch) + 1.0f) / n;
    return span >= ctr - space_ * hw && span <= ctr + space_ * hw;
}

modulator_output cv_channel_decoder::tick(double /*beat*/, float tick_rate_hz) {
    const float span = read_span();

    // Velocity: |dSPAN/dt| normalised so a full [-1,1] sweep in 1s = 1.0.
    // On the first tick there is no meaningful previous value — emit 0.
    float velocity = 0.0f;
    if (!first_tick_ && tick_rate_hz > 0.0f)
        velocity = std::min(1.0f, std::abs(span - prev_span_) * tick_rate_hz * 0.5f);
    first_tick_ = false;
    prev_span_  = span;

    // Compute channel bitmap.
    uint32_t bitmap = 0;
    for (int i = 0; i < channels_; ++i)
        if (channel_active(i, span))
            bitmap |= (1u << i);

    if (clocked_) {
        if (clock_pending_) {
            latched_state_ = bitmap;
            clock_pending_ = false;
        }
        bitmap = latched_state_;
    }

    modulator_output out;
    out.cv    = velocity;
    out.aux   = span;
    out.gate  = (bitmap != 0);
    out.state = bitmap;
    return out;
}

void cv_channel_decoder::update(std::string_view key, float value) {
    if      (key == "span")       span_          = std::clamp(value, -1.0f, 1.0f);
    else if (key == "space")      space_         = std::clamp(value,  0.0f,  2.0f);
    else if (key == "clocked")    clocked_       = (value > 0.5f);
    else if (key == "clock_tick" && value > 0.5f) clock_pending_ = true;
}

} // namespace nomos::rt
