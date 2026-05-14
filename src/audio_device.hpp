// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <RtAudio.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace nomos::rt {

struct audio_device_config {
    unsigned int device_id{0}; // 0 = default output
    uint32_t     out_channels{2};
    uint32_t     in_channels{0};
    double       sample_rate{48000.0};
    uint32_t     buffer_frames{256};
};

// Callback invoked from the RtAudio thread each block.
// Non-interleaved float32: channels[c] points to buffer_frames samples.
// input_channels is null when in_channels == 0.
using audio_callback_t =
    std::function<void(float** output_channels, const float* const* input_channels, uint32_t out_ch,
                       uint32_t in_ch, uint32_t nframes, double stream_time)>;

class audio_device {
  public:
    audio_device();
    ~audio_device();

    audio_device(const audio_device&)            = delete;
    audio_device& operator=(const audio_device&) = delete;

    // List available devices to stderr.
    static void list_devices();

    bool open(const audio_device_config& cfg, audio_callback_t cb);
    bool start();
    void stop();
    void close();

    bool is_open() const noexcept;
    bool is_running() const noexcept;

  private:
    static int rt_callback(void* output, void* input, unsigned int nframes, double stream_time,
                           RtAudioStreamStatus status, void* user_data);

    RtAudio          rt_;
    audio_callback_t cb_;
    uint32_t         out_ch_{0};
    uint32_t         in_ch_{0};

    // Non-interleaved channel pointer scratch arrays (avoids alloc per callback).
    std::vector<float*>       out_ptrs_;
    std::vector<const float*> in_ptrs_;
};

} // namespace nomos::rt
