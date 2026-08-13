<!--
SPDX-FileCopyrightText: 2025-2026 nomos-studio contributors

SPDX-License-Identifier: LGPL-2.1-or-later
-->

# nomos-rt IPC frame protocol

**Owning component:** `nomos-rt` — the real-time substrate (Link peer, MIDI/OSC/CV
I/O, scheduling). This is the protocol nomos-rt exposes to its clients.

**Normative source of truth:** `include/nomos/rt/ipc.hpp`. The opcode constants and
their payload comments in that header are authoritative; this document explains and
groups them. If the two disagree, the header wins — update this doc.

## Purpose

nomos-rt runs as a separate process from the compositional engine. Clients drive it —
and receive telemetry from it — by exchanging framed messages over a byte stream. The
message *payloads* are EDN (Extensible Data Notation), so the protocol is
self-describing and language-neutral: any client that can open the socket, write the
8-byte header, and serialise/parse EDN can speak it. This is a deliberately
**arms-length** interface — clients and nomos-rt are independent programs, not linked
libraries.

## Boundary parties

| Side | Who | Role |
|---|---|---|
| Server | `nomos-rt` (embedded in `aion` / `kairos`) | owns the Link session, MIDI/OSC/CV I/O, the RT scheduler and modulator engine |
| Client | `nous` (`nous.kairos`, JVM/Clojure) | primary client — mirrors the opcodes as `MSG-*` constants in `kairos.clj` |
| Client | `aion`, `kairos` (C++) | in-process and peer clients |

## Transport

A duplex byte stream — a Unix domain socket (local) or TCP. Framing and semantics are
transport-agnostic; only in-order, reliable byte delivery is assumed.

## Framing

Every message is an **8-byte header** followed by a payload:

```
┌───────────────────────────┬────────┬──────────────┬───────────────────┐
│ payload_len : uint32 (BE)  │ type:u8│ reserved[3]  │ payload (payload_len bytes) │
└───────────────────────────┴────────┴──────────────┴───────────────────┘
        4 bytes                 1 byte     3 bytes            payload_len bytes
```

- `payload_len` — byte count of the **payload only** (excludes the 8-byte header),
  in **network byte order (big-endian)**. Little-endian hosts must byte-swap.
- `type` — the message opcode (see table below).
- `reserved[3]` — must be zero; reserved for future use.
- `payload` — `payload_len` bytes of UTF-8 **EDN**, or empty when a message carries no
  payload. EDN encoding/decoding on the C++ side is provided by the `edn-cpp` library.

The header struct is defined for direct `memcpy` from the wire
(`nomos::rt::ipc::header`, `static_assert`-ed to 8 bytes).

## Message vocabulary

Opcodes are grouped by concern. "Dir" is the usual direction: **→rt** = client to
nomos-rt (command); **→client** = nomos-rt pushed to connected clients (telemetry/
response). Payloads shown are the EDN shapes from the header comments.

**Core vs node extensions.** The base `rt_control_thread` handles the shared vocabulary
(sessions, notes, MIDI/OSC out, scheduling, modulators, telemetry). Node-specific opcodes
are handled by a `dispatch_extension` override in the node's `rt_control_thread` subclass:
**kairos** (`kairos::control_thread`) handles the plugin-graph / WASM opcodes; **aion**
(`aion_control_thread`) handles `msg_route_set`. Exactly one node runs in a given
deployment, and external MIDI/OSC out is delivered by the shared substrate either way, so
clients need not know which node is present.

### Session & sources (`0x30`–`0x33`)
| Op | Name | Dir | Payload |
|----|------|-----|---------|
| `0x30` | `msg_tx_log` | →rt | tx_log entry; EDN keyword source id |
| `0x31` | `msg_session_open` | →rt | — |
| `0x32` | `msg_session_close` | →rt | — |
| `0x33` | `msg_register_source` | →rt | EDN keyword id + name + description |

### Plugin graph (`0x34`–`0x37`, `0x44`, `0x53`)
| Op | Name | Dir | Payload |
|----|------|-----|---------|
| `0x34` | `msg_graph_load` | →rt | EDN plugin-graph description |
| `0x35` | `msg_graph_reset` | →rt | — (tear down current graph) |
| `0x36` | `msg_plugin_list_req` | →rt | EDN `{:extra-paths [...]}` or empty |
| `0x37` | `msg_plugin_list_resp` | →client | EDN `[{:id :name :vendor :version :path} ...]` |
| `0x44` | `msg_wasm_hot_swap` | →rt | EDN `{:node-id :kw :wasm-path "..."}` (gapless swap) |
| `0x53` | `msg_graph_load_ack` | →client | EDN `{:nodes N}` after a successful load |

### Link transport (`0x38`–`0x3A`)
| Op | Name | Dir | Payload |
|----|------|-----|---------|
| `0x38` | `msg_link_set_tempo` | →rt | EDN `{:bpm 120.0}` (propose to the Link session) |
| `0x39` | `msg_link_start_transport` | →rt | — (isPlaying = true) |
| `0x3A` | `msg_link_stop_transport` | →rt | — (isPlaying = false) |

### Parameters, notes & MIDI in (`0x40`–`0x43`)
| Op | Name | Dir | Payload |
|----|------|-----|---------|
| `0x40` | `msg_param_set` | →rt | EDN path + pending tuple |
| `0x41` | `msg_note_on` | →rt | EDN note-on event → `ipc_in_queue` |
| `0x42` | `msg_note_off` | →rt | EDN note-off event → `ipc_in_queue` |
| `0x43` | `msg_midi_in` | →rt | EDN raw MIDI bytes → `ipc_in_queue` |

### Scheduling & modulators (`0x45`–`0x48`)
| Op | Name | Dir | Payload |
|----|------|-----|---------|
| `0x45` | `msg_schedule_bundle` | →rt | EDN `{:at-beat D :events [ev …]}`. Each `ev` is a **note** — `{:at-tick N :type :note-on/:note-off :key K :velocity …}` — or an **OSC datagram** — `{:at-tick N :type :osc :host :port :address :args [...]}`. Notes and OSC may be mixed in one bundle; the same `:at-tick` fires on the same tick. Each event's beat = `:at-beat + :at-tick / 24.0`. |
| `0x46` | `msg_modulator_start` | →rt | EDN `{:id :kw :type :slope\|... + params}` |
| `0x47` | `msg_modulator_stop` | →rt | EDN `{:id :kw}` |
| `0x48` | `msg_modulator_update` | →rt | EDN `{:id :kw :key "rate" :value 0.5}` |

### MIDI out — channel voice & tuning (`0x49`–`0x4D`)
| Op | Name | Dir | Payload |
|----|------|-----|---------|
| `0x49` | `msg_cc` | →rt | EDN `{:port N :channel N :cc N :value N}` |
| `0x4A` | `msg_pitch_bend` | →rt | EDN `{:port N :channel N :value N}` (14-bit signed, −8192–8191, centre 0) |
| `0x4B` | `msg_chan_pressure` | →rt | EDN `{:port N :channel N :value N}` (0–127) |
| `0x4C` | `msg_sysex` | →rt | EDN `{:port N :data [b0 … bN]}` (raw SysEx, F0…F7) |
| `0x4D` | `msg_mts` | →rt | EDN `{:port N :tuning {midi→hz} :tuning-prog N :device-id N\|:all}` → 408-byte MTS Bulk Dump SysEx |

### Telemetry & routing pushed by nomos-rt (`0x50`–`0x52`, `0x54`)
| Op | Name | Dir | Payload |
|----|------|-----|---------|
| `0x50` | `msg_tick` | →client | EDN `{:beat D :tick-n N :mods {:id {:cv F :aux F :gate B :gate2 B} ...}}` — each 24 PPQN tick; `:mods` omitted when empty |
| `0x51` | `msg_midi_event` | →client | EDN `{:port N :channel N :data [status b1 b2]}` — pushed by aion on hardware MIDI in |
| `0x52` | `msg_route_set` | →rt | EDN `{:midi-routes [...] :mod-routes [...]}` — replace the aion routing matrix. Handled by `aion_control_thread::dispatch_extension` (a node extension), which applies it to the `RoutingMatrix` |
| `0x54` | `msg_midi_diag` | →client | EDN `{:bytes [...]}` — pushed by aion on every MIDI-out send, before RtMidi (fires with no port open — CI path) |

### Routed REPL eval (`0x55`–`0x56`)
| Op | Name | Dir | Payload |
|----|------|-----|---------|
| `0x55` | `msg_repl_eval` | ↔ | EDN `{:dest :fennel\|:nous\|:vcvrack-tty :payload "…" :id "…"}` — routed eval; response returns as `msg_repl_eval` (socket) or `msg_repl_eval_response` (VCVRack tty path). Result: `{:id "…" :result <edn>}` or `{:id "…" :error "…"}` |
| `0x56` | `msg_repl_eval_response` | →client | EDN `{:result <val> :error nil\|"…"}` — pushed to the VCVRack TTY screen |

### OSC out (`0x57`)
| Op | Name | Dir | Payload |
|----|------|-----|---------|
| `0x57` | `msg_osc` | →rt | EDN `{:host "…" :port N :address "…" :args [v0 v1 …]}` — send an OSC message to an external UDP endpoint **immediately**. Arg types are inferred from EDN: int→`i32`, float→`f32`, string→OSC-string. The node encodes the datagram and sends it via its `osc_server`, so OSC output rides the RT substrate like MIDI-out — GC-immune and node-agnostic (whichever node runs delivers it). The **beat-scheduled** form is an `:osc` event inside `msg_schedule_bundle` (`0x45`), which fires at an exact Link beat on the RT thread, in sync with co-scheduled notes. |

### Analysis taps (`0x58`)
| Op | Name | Dir | Payload |
|----|------|-----|---------|
| `0x58` | `msg_tap` | →client | EDN `{:epoch N :taps {:name value …}}` — pushed by the node draining the kairos **tap bus** (`CLAP_EXT_KAIROS_TAP_BUS`): named analysis/probe values (spectral peaks, envelope, level, tuner cents) that a plugin exposes. Values are **snapshotted on the audio thread** (right after `process()`, into a lock-free queue — RT-safe, never read cross-thread), then a telemetry thread joins them with the tap-schema names, builds the EDN, and pushes at **`tap-push-rate-hz`** (§25 config, default 30 Hz). `:epoch` tracks schema generation (increments on graph reload); `:taps` is empty when no tap bus is present. Clients land the values on `[:tap …]` ctrl-tree paths — the studio observing its own signal state (the input-side dual of the ctrl-tree → nomos-rt output mount). See `nomos-studio/plans/tap-ipc-bridge-design.md`. |

> The `0x00`–`0x2F` range is unused — a vestige of pre-`nomos-rt` (cljseq-rt) numbering.
> No opcodes are defined there; the vocabulary begins at `0x30`. The range is left free
> and may be reclaimed later.

## Stability & versioning

Opcodes are append-only: assigned values are stable, new messages take the next free
code. There is no in-band version negotiation — client and server agree by both
tracking `ipc.hpp`. `reserved[3]` is held for a future version/flags field. The
`nous.kairos` `MSG-*` constants mirror this header and must be kept in lockstep.

## Reimplementing a client

A conforming client needs only to: open the stream; for each message write the 8-byte
big-endian header then the EDN payload; and, for pushed messages, read the header,
read `payload_len` bytes, and parse the EDN. No nomos-rt code is linked — this is the
whole contract. See `nous/src/nous/kairos.clj` for a reference client.

## Related

- Product boundary index: `nomos-studio/doc/component-boundaries.md`
- EDN payload codec: the `edn-cpp` component.
