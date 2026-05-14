// SPDX-License-Identifier: GPL-2.0-or-later
#include "audio_device.hpp"

#include <cstdio>

namespace nomos::rt {

audio_device::audio_device() = default;

audio_device::~audio_device() {
    close();
}

void audio_device::list_devices() {
    RtAudio    rt;
    const auto ids = rt.getDeviceIds();
    std::fprintf(stderr, "[audio] %zu device(s):\n", ids.size());
    for (const unsigned int id : ids) {
        const auto info = rt.getDeviceInfo(id);
        std::fprintf(stderr, "  [%u] %s  (out=%u in=%u sr=", id, info.name.c_str(),
                     info.outputChannels, info.inputChannels);
        for (std::size_t i = 0; i < info.sampleRates.size(); ++i) {
            if (i)
                std::fprintf(stderr, ",");
            std::fprintf(stderr, "%u", info.sampleRates[i]);
        }
        std::fprintf(stderr, ")\n");
    }
}

bool audio_device::open(const audio_device_config& cfg, audio_callback_t cb) {
    cb_     = std::move(cb);
    out_ch_ = cfg.out_channels;
    in_ch_  = cfg.in_channels;

    out_ptrs_.resize(out_ch_);
    in_ptrs_.resize(in_ch_);

    RtAudio::StreamParameters out_params;
    out_params.deviceId     = cfg.device_id ? cfg.device_id : rt_.getDefaultOutputDevice();
    out_params.nChannels    = cfg.out_channels;
    out_params.firstChannel = 0;

    RtAudio::StreamParameters* in_params_ptr = nullptr;
    RtAudio::StreamParameters  in_params;
    if (cfg.in_channels > 0) {
        in_params.deviceId     = cfg.device_id ? cfg.device_id : rt_.getDefaultInputDevice();
        in_params.nChannels    = cfg.in_channels;
        in_params.firstChannel = 0;
        in_params_ptr          = &in_params;
    }

    RtAudio::StreamOptions options;
    options.flags = RTAUDIO_NONINTERLEAVED | RTAUDIO_MINIMIZE_LATENCY;

    unsigned int buffer_frames = cfg.buffer_frames;

    const RtAudioErrorType err = rt_.openStream(&out_params, in_params_ptr, RTAUDIO_FLOAT32,
                                                static_cast<unsigned int>(cfg.sample_rate),
                                                &buffer_frames, &rt_callback, this, &options);

    if (err != RTAUDIO_NO_ERROR) {
        std::fprintf(stderr, "[audio] openStream failed: %s\n", rt_.getErrorText().c_str());
        return false;
    }

    std::fprintf(stderr, "[audio] opened device %u  out=%u in=%u sr=%.0f frames=%u\n",
                 out_params.deviceId, out_ch_, in_ch_, cfg.sample_rate, buffer_frames);
    return true;
}

bool audio_device::start() {
    const RtAudioErrorType err = rt_.startStream();
    if (err != RTAUDIO_NO_ERROR) {
        std::fprintf(stderr, "[audio] startStream failed: %s\n", rt_.getErrorText().c_str());
        return false;
    }
    return true;
}

void audio_device::stop() {
    if (rt_.isStreamRunning())
        rt_.stopStream();
}

void audio_device::close() {
    stop();
    if (rt_.isStreamOpen())
        rt_.closeStream();
}

bool audio_device::is_open() const noexcept {
    return rt_.isStreamOpen();
}
bool audio_device::is_running() const noexcept {
    return rt_.isStreamRunning();
}

int audio_device::rt_callback(void* output, void* input, unsigned int nframes, double stream_time,
                              RtAudioStreamStatus /*status*/, void* user_data) {
    auto* self = static_cast<audio_device*>(user_data);

    auto* out_buf = static_cast<float*>(output);
    auto* in_buf  = static_cast<float*>(input);

    // Build non-interleaved channel pointer arrays.
    for (uint32_t c = 0; c < self->out_ch_; ++c)
        self->out_ptrs_[c] = out_buf + c * nframes;

    if (in_buf) {
        for (uint32_t c = 0; c < self->in_ch_; ++c)
            self->in_ptrs_[c] = in_buf + c * nframes;
    }

    if (self->cb_) {
        self->cb_(self->out_ptrs_.data(), in_buf ? self->in_ptrs_.data() : nullptr, self->out_ch_,
                  self->in_ch_, static_cast<uint32_t>(nframes), stream_time);
    }

    return 0; // 0 = continue stream
}

} // namespace nomos::rt
