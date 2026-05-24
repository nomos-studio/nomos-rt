// SPDX-License-Identifier: LGPL-2.1-or-later
#include "faust_modulator.hpp"

#include <algorithm>
#include <cmath>

namespace nomos::rt {

faust_modulator::faust_modulator(std::unique_ptr<dsp_block> dsp)
    : dsp_(std::move(dsp))
{
    for (int i = 0; i < kBufSize; ++i)
        ptrs_[i] = &samples_[i];
}

void faust_modulator::ensure_init(float tick_rate_hz) {
    if (!initialised_ || std::fabs(tick_rate_hz - current_rate_) > 1.0f) {
        dsp_->init(tick_rate_hz);
        current_rate_ = tick_rate_hz;
        initialised_  = true;
    }
}

modulator_output faust_modulator::tick(double beat, float tick_rate_hz) {
    ensure_init(tick_rate_hz);

    const float beat_phase = static_cast<float>(beat - std::floor(beat));
    dsp_->set_param("beat", beat_phase);

    const int n = std::min(dsp_->num_outputs(), kBufSize);
    std::fill(samples_, samples_ + kBufSize, 0.0f);
    dsp_->process(ptrs_);

    modulator_output result;
    for (int i = 0; i < n; ++i)
        result.outputs[i] = samples_[i];

    result.cv    = result.outputs[0];
    result.aux   = (n > 1) ? result.outputs[1] : 0.0f;
    result.gate  = (n > 2) ? result.outputs[2] > 0.5f : false;
    result.gate2 = (n > 3) ? result.outputs[3] > 0.5f : false;

    uint32_t state = 0;
    for (int i = 0; i < n; ++i)
        if (result.outputs[i] > 0.5f)
            state |= (1u << i);
    result.state = state;

    return result;
}

void faust_modulator::update(std::string_view key, float value) {
    dsp_->set_param(key, value);
}

} // namespace nomos::rt
