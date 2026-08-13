// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <cstddef>
#include <cstdint>

namespace nomos::rt::ipc {

// Wire frame: [uint32_t payload_len][uint8_t type][uint8_t
// reserved[3]][payload…] payload_len is the byte count of the payload only —
// excludes the 8-byte header.
constexpr std::size_t header_size = 8;

// Message type codes.
// 0x3x — session / graph block (new in kairos; extend the sidecar 0x0x–0x2x
// block). 0x40  — parameter set.
constexpr uint8_t msg_tx_log          = 0x30; // tx_log entry; EDN keyword source id
constexpr uint8_t msg_session_open    = 0x31;
constexpr uint8_t msg_session_close   = 0x32;
constexpr uint8_t msg_register_source = 0x33; // EDN keyword id + name + description
constexpr uint8_t msg_graph_load      = 0x34; // EDN plugin graph description
constexpr uint8_t msg_graph_reset     = 0x35; // tear down current graph
constexpr uint8_t msg_plugin_list_req =
    0x36; // EDN {:extra-paths [...]} or empty — request plugin discovery
constexpr uint8_t msg_plugin_list_resp =
    0x37; // EDN [{:id "..." :name "..." :vendor "..." :version "..." :path
          // "..."} ...]
constexpr uint8_t msg_link_set_tempo = 0x38; // EDN {:bpm 120.0} — propose tempo to the Link session
constexpr uint8_t msg_link_start_transport =
    0x39; // no payload — set isPlaying=true in the Link session state
constexpr uint8_t msg_link_stop_transport =
    0x3A; // no payload — set isPlaying=false in the Link session state
constexpr uint8_t msg_param_set = 0x40; // EDN path + pending tuple
constexpr uint8_t msg_note_on   = 0x41; // EDN note-on event  → ipc_in_queue
constexpr uint8_t msg_note_off  = 0x42; // EDN note-off event → ipc_in_queue
constexpr uint8_t msg_midi_in   = 0x43; // EDN raw MIDI bytes → ipc_in_queue
constexpr uint8_t msg_wasm_hot_swap =
    0x44; // EDN {:node-id :kw :wasm-path "..."}  → gapless WASM swap
constexpr uint8_t msg_schedule_bundle = 0x45; // EDN {:at-beat D :events [{:at-tick N :type
                                              // :note-on/:note-off :key K ...}]}
constexpr uint8_t msg_modulator_start =
    0x46; // EDN {:id :kw :type :slope|... + params} — start an RT modulator
constexpr uint8_t msg_modulator_stop = 0x47; // EDN {:id :kw} — stop a named RT modulator
constexpr uint8_t msg_modulator_update =
    0x48; // EDN {:id :kw :key "rate" :value 0.5} — update a modulator parameter
constexpr uint8_t msg_cc         = 0x49; // EDN {:port N :channel N :cc N :value N} — send MIDI CC
constexpr uint8_t msg_pitch_bend = 0x4A; // EDN {:port N :channel N :value N} —
                                         // 14-bit signed, -8192–8191, centre=0
constexpr uint8_t msg_chan_pressure = 0x4B; // EDN {:port N :channel N :value N} — 0–127
constexpr uint8_t msg_sysex = 0x4C; // EDN {:port N :data [b0 b1 … bN]} — raw SysEx bytes (F0…F7)
constexpr uint8_t msg_mts =
    0x4D; // EDN {:port N :tuning {midi→hz} :tuning-prog N :device-id N|:all}
          //   assembles and sends a 408-byte MTS Bulk Dump SysEx
constexpr uint8_t msg_tick =
    0x50; // EDN {:beat D :tick-n N :mods {:id {:cv F :aux F :gate B :gate2 B}
          // ...}} pushed to connected clients on each 24 PPQN tick; :mods
          // omitted when empty
constexpr uint8_t msg_midi_event = 0x51; // EDN {:port N :channel N :data [status b1 b2]} pushed by
                                         // aion on hw MIDI in
constexpr uint8_t msg_route_set = 0x52; // EDN {:midi-routes [...] :mod-routes [...]} — replace the
                                        // aion routing matrix
constexpr uint8_t msg_graph_load_ack =
    0x53; // EDN {:nodes N} — pushed to client after a successful msg_graph_load
constexpr uint8_t msg_midi_diag =
    0x54; // EDN {:bytes [b0 b1 …]} — pushed by aion on every MIDI output send,
          // before bytes reach RtMidi; fires even with no port open (CI path)

constexpr uint8_t msg_repl_eval =
    0x55; // EDN {:dest :fennel|:nous|:vcvrack-tty :payload "…" :id "…"}
          // → routed eval; response pushed back as msg_repl_eval (socket) or
          //   msg_repl_eval_response (VCVRack tty_sink path).
          //   {:id "…" :result <edn-value>} or {:id "…" :error "…"}

constexpr uint8_t msg_repl_eval_response =
    0x56; // EDN {:result <val> :error nil|"…"} — pushed to VCVRack TTY screen
          // via tty_sink → VCVBridgeModule → shm → TtyModule::parse_ctrl_response

constexpr uint8_t msg_osc =
    0x57; // EDN {:host "…" :port N :address "…" :args [v0 v1 …]} — send an OSC
          // message to an external UDP endpoint (immediate). Arg types inferred
          // from EDN: int64→i32, double→f32, string→OSC-string. Routed through
          // the node's osc_server so scheduled/immediate OSC gains the same
          // GC-immunity + node-agnostic delivery as MIDI-out.

constexpr uint8_t msg_tap =
    0x58; // EDN {:epoch N :taps {:name value …}} — pushed by the node draining the
          // kairos tap bus (CLAP_EXT_KAIROS_TAP_BUS): named analysis/probe values
          // (spectral peaks, envelope, level, tuner cents) sampled on the audio
          // thread, joined with the tap schema names, throttled to
          // tap-push-rate-hz. Consumers land it on [:tap …] ctrl-tree paths —
          // the studio observing its own signal state. :epoch tracks schema
          // generation; taps map is empty when no tap bus is present.

// Header layout.  Laid out for direct memcpy from the wire; fields in network
// byte order (big-endian) — callers must byte-swap payload_len on little-endian
// hosts.
struct alignas(4) header {
    uint32_t payload_len{0};
    uint8_t  type{0};
    uint8_t  reserved[3]{};
};

static_assert(sizeof(header) == header_size, "IPC header must be exactly 8 bytes");

} // namespace nomos::rt::ipc
