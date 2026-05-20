// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <cstddef>
#include <cstdint>

namespace nomos::rt::ipc {

// Wire frame: [uint32_t payload_len][uint8_t type][uint8_t reserved[3]][payload…]
// payload_len is the byte count of the payload only — excludes the 8-byte header.
constexpr std::size_t header_size = 8;

// Message type codes.
// 0x3x — session / graph block (new in kairos; extend the sidecar 0x0x–0x2x block).
// 0x40  — parameter set.
constexpr uint8_t msg_tx_log          = 0x30; // tx_log entry; EDN keyword source id
constexpr uint8_t msg_session_open    = 0x31;
constexpr uint8_t msg_session_close   = 0x32;
constexpr uint8_t msg_register_source = 0x33; // EDN keyword id + name + description
constexpr uint8_t msg_graph_load      = 0x34; // EDN plugin graph description
constexpr uint8_t msg_graph_reset     = 0x35; // tear down current graph
constexpr uint8_t msg_param_set       = 0x40; // EDN path + pending tuple
constexpr uint8_t msg_note_on         = 0x41; // EDN note-on event  → ipc_in_queue
constexpr uint8_t msg_note_off        = 0x42; // EDN note-off event → ipc_in_queue
constexpr uint8_t msg_midi_in         = 0x43; // EDN raw MIDI bytes → ipc_in_queue
constexpr uint8_t msg_wasm_hot_swap =
    0x44; // EDN {:node-id :kw :wasm-path "..."}  → gapless WASM swap
constexpr uint8_t msg_schedule_bundle =
    0x45; // EDN {:at-beat D :events [{:at-tick N :type :note-on/:note-off :key K ...}]}
constexpr uint8_t msg_modulator_start =
    0x46; // EDN {:id :kw :type :slope|... + params} — start an RT modulator
constexpr uint8_t msg_modulator_stop =
    0x47; // EDN {:id :kw} — stop a named RT modulator
constexpr uint8_t msg_modulator_update =
    0x48; // EDN {:id :kw :key "rate" :value 0.5} — update a modulator parameter
constexpr uint8_t msg_tick =
    0x50; // EDN {:beat D :tick-n N :mods {:id {:cv F :aux F :gate B :gate2 B} ...}}
          // pushed to connected clients on each 24 PPQN tick; :mods omitted when empty

// Header layout.  Laid out for direct memcpy from the wire; fields in network byte
// order (big-endian) — callers must byte-swap payload_len on little-endian hosts.
struct alignas(4) header {
    uint32_t payload_len{0};
    uint8_t  type{0};
    uint8_t  reserved[3]{};
};

static_assert(sizeof(header) == header_size, "IPC header must be exactly 8 bytes");

} // namespace nomos::rt::ipc
