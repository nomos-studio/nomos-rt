// SPDX-FileCopyrightText: 2025-2026 nomos-studio contributors
//
// SPDX-License-Identifier: EPL-2.0

#pragma once

#include <cstdint>
#include <cstdlib>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace nomos::rt {

// All CLI arguments common to every nomos-rt process (aion, kairos, …).
// Set process-specific defaults before calling parse_common_args.
struct common_args {
    std::string  socket_path; // e.g. "/tmp/aion.sock"
    std::string  db_path   = "nomos.db";
    double       bpm       = 120.0;
    int          midi_port = -1; // -1 = not specified by index
    std::string  midi_port_name; // non-empty takes precedence over index
    int          midi_in_port = -1;
    std::string  midi_in_port_name;
    uint16_t     osc_port        = 9000;
    unsigned int audio_device    = 0;
    uint32_t     audio_out_ch    = 2;
    uint32_t     audio_in_ch     = 0;
    uint32_t     block_size      = 256; // audio buffer / CLAP block size in frames
    bool         no_audio        = false;
    bool         list_audio_devs = false;
    bool         version         = false;
};

// Parse argv[1..argc-1], consuming recognised common flags into `args`.
// Unknown flags (and their values, where detectable) are returned as remainder
// for the caller to handle as process-specific args.
inline std::vector<std::string_view> parse_common_args(int argc, char* argv[], common_args& args) {
    std::vector<std::string_view> rem;

    auto parse_midi = [&](int& idx, std::string& name, int i, char* argv[]) {
        std::string_view v{argv[i]};
        char*            end;
        long             n = std::strtol(v.data(), &end, 10);
        if (end != v.data() && *end == '\0')
            idx = static_cast<int>(n);
        else
            name = std::string{v};
    };

    for (int i = 1; i < argc; ++i) {
        const std::string_view a{argv[i]};
        const bool             has_next = (i + 1 < argc);

        if (a == "--socket" && has_next)
            args.socket_path = argv[++i];
        else if (a == "--db" && has_next)
            args.db_path = argv[++i];
        else if (a == "--bpm" && has_next)
            args.bpm = std::atof(argv[++i]);
        else if (a == "--midi-port" && has_next)
            parse_midi(args.midi_port, args.midi_port_name, ++i, argv);
        else if (a == "--midi-in-port" && has_next)
            parse_midi(args.midi_in_port, args.midi_in_port_name, ++i, argv);
        else if (a == "--osc-port" && has_next)
            args.osc_port = static_cast<uint16_t>(std::atoi(argv[++i]));
        else if (a == "--audio-device" && has_next)
            args.audio_device = static_cast<unsigned int>(std::atoi(argv[++i]));
        else if (a == "--audio-out-ch" && has_next)
            args.audio_out_ch = static_cast<uint32_t>(std::atoi(argv[++i]));
        else if (a == "--audio-in-ch" && has_next)
            args.audio_in_ch = static_cast<uint32_t>(std::atoi(argv[++i]));
        else if (a == "--block-size" && has_next)
            args.block_size = static_cast<uint32_t>(std::atoi(argv[++i]));
        else if (a == "--no-audio")
            args.no_audio = true;
        else if (a == "--list-audio-devices")
            args.list_audio_devs = true;
        else if (a == "--version")
            args.version = true;
        else {
            rem.push_back(a);
            // If the next token doesn't look like a flag, treat it as this
            // flag's value so the caller receives both tokens in order.
            if (has_next && argv[i + 1][0] != '-')
                rem.push_back(argv[++i]);
        }
    }
    return rem;
}

// Print help lines for all common flags, including their defaults from `args`.
// Call from each process's --help handler before appending process-specific lines.
inline void print_common_args_help(const common_args& defaults, std::ostream& out) {
    out << "  --socket <path>            Unix domain socket          (default: "
        << defaults.socket_path << ")\n"
        << "  --db <path>                txlog database path         (default: " << defaults.db_path
        << ")\n"
        << "  --bpm <bpm>                Initial Link tempo          (default: " << defaults.bpm
        << ")\n"
        << "  --midi-port <n|name>       MIDI output port index or name substring\n"
        << "  --midi-in-port <n|name>    MIDI input port index or name substring\n"
        << "  --osc-port <n>             UDP OSC listen port         (default: "
        << defaults.osc_port << ")\n"
        << "  --audio-device <id>        RtAudio device id           (default: "
        << defaults.audio_device << ", see --list-audio-devices)\n"
        << "  --audio-out-ch <n>         Audio output channels       (default: "
        << defaults.audio_out_ch << ")\n"
        << "  --audio-in-ch <n>          Audio input channels        (default: "
        << defaults.audio_in_ch << ")\n"
        << "  --block-size <n>           Audio buffer / CLAP block   (default: "
        << defaults.block_size << ")\n"
        << "  --no-audio                 Skip audio device\n"
        << "  --list-audio-devices       Print available audio devices and exit\n"
        << "  --version                  Print version and exit\n";
}

} // namespace nomos::rt
