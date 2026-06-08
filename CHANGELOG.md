# Changelog

All notable changes to nomos-rt are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

---

## [Unreleased]

---

## [0.1.0] — 2026-06-07

### Added

#### IPC substrate (`nomos::rt::rt_control_thread`)

- Unix-domain socket server; accepts EDN-framed messages in the
  `[uint32 len][uint8 type][3 reserved][payload]` wire format.
- Base handles: `SESSION-OPEN/CLOSE`, `REGISTER-SOURCE`, `TX-LOG`,
  `PARAM-SET`, `NOTE-ON/OFF`, `MIDI-IN`, `CC`, `PITCH-BEND`,
  `CHAN-PRESSURE`, `SYSEX`, `MTS`, `MODULATOR-START/STOP/UPDATE`,
  `SCHEDULE-BUNDLE`, `LINK-SET-TEMPO`, `LINK-START/STOP-TRANSPORT`.
- `dispatch_extension(conn_fd, msg, sess)` — virtual hook for derived
  classes to handle runtime-specific message types.
- `push_frame(type, payload)` — thread-safe outbound frame write to the
  connected client.
- `MSG-TICK` (0x50) — 24 PPQN tick with beat position and modulator
  outputs; pushed by the event loop, not the base class.
- `MSG-ROUTE-SET` (0x52) — routing matrix update (handled by aion's
  `aion_control_thread` subclass).
- `MSG-MIDI-EVENT` (0x51) — hardware MIDI input push.
- `MSG-PLUGIN-LIST-REQ/RESP` (0x36/0x37) — plugin discovery round-trip.

#### Modulator engine (`nomos::rt::modulator_engine`)

- Zero-alloc RCU engine; modulators tick in stable insertion order.
- Built-in modulator types:
  - **`:slope`** — periodic LFO with shape, slope, smoothness, depth, bipolar controls.
  - **`:segment`** — multi-segment function generator (up to 36 segments:
    ramp, step, hold, alt).
  - **`:slew`** — lag processor / function generator with rise/fall/cycle.
  - **`:shift-register`** — clocked shift register with four feedback
    modes (Turing, LFSR, Rungler, open).
  - **`:fractal`** — fBm multi-octave CV via fractional Brownian motion.
  - **`:stochastic`** — probabilistic sample-and-hold with déjà vu
    (pattern repetition probability).
  - **`:graph`** — s-expression control graph interpreter supporting
    arithmetic, conditional, cross-modulator references, and multi-output
    `{:cv expr :gate expr ...}` maps.
  - **NLC-derived** — `:cv-channel-decoder`, `:divine-cmos`, `:statues`,
    `:cipher`, `:bools-ring` (Boolean logic/shift attractor banks).
  - **Attractor-group** — `:sloth-chaos`, `:squid-axon`, `:genie`,
    `:lets-splosh` (chaotic CV sources).
- `modulator_output` struct: `cv`, `aux`, `gate`, `gate2`, `state`,
  `outputs[16]` (extended CV bank).
- `last_output(id)` — snapshot of the previous tick for cross-modulator
  routing.

#### `faust_modulator` — block-rate alembic patch execution

- Runs a Faust-compiled `.wasm` graph at modulator tick rate (not audio
  rate); bridges the alembic DSL into the nomos-rt modulator engine.

#### Event scheduler (`nomos::rt::event_scheduler`)

- Staging queue (main thread) → event queue (event thread); tick-accurate
  dispatch at 24 PPQN.  Handles `MSG-SCHEDULE-BUNDLE` note events.

#### Shared infrastructure

- `spsc_queue<T, N>` — lock-free single-producer single-consumer ring
  buffer; used for all cross-thread event delivery.
- `rcu.hpp` — urcu-bp read-copy-update primitives for gapless DSP swap.
- `result<T, E>` — `expected`-style monad used throughout the C++ API.
- `param_event` / `input_event_queue` — typed CLAP event wrappers.
- `session` / `ipc_channel` / `ipc.hpp` — message type constants and
  framing helpers shared by kairos and aion.
- `time_identity` — beat/bar quantise, snap, and smooth policy.
- `link_peer` — Ableton Link peer (header exposed to executables).
- `midi_io` / `osc_server` / `audio_device` — hardware I/O helpers
  (exposed via `nomos::rt-exe-headers` INTERFACE target; GPL-2.0-or-later).

### Fixed

- `read_message` returns `eof` on clean peer close instead of `io_error`.

### Changed

- Extracted from kairos as `cljseq-rt`, then renamed to `nomos-rt`
  with namespace `nomos::rt` throughout.
- txlog-cpp dependency pinned to `b0a661b` for reproducible builds.
