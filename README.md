# nomos-rt

Shared C++ runtime substrate for the [nomos-studio](https://github.com/nomos-studio) ecosystem. Provides the common foundation that [kairos](https://github.com/nomos-studio/kairos) (CLAP container) and [aion](https://github.com/nomos-studio/aion) (session peer) are built on.

## What's in here

| Component | Headers | Description |
|-----------|---------|-------------|
| IPC channel | `nomos/rt/ipc.hpp`, `ipc_channel.hpp` | Unix domain socket framing; 8-byte header + EDN payload |
| Session / txlog | `nomos/rt/session.hpp` | Opens a txlog SQLite database; maps SESSION-OPEN/CLOSE/REGISTER-SOURCE |
| Control thread base | `nomos/rt/rt_control_thread.hpp` | Listens on the IPC socket; dispatches common message types; CRTP extension point for CLAP/aion-specific messages |
| Time identity | `nomos/rt/time_identity.hpp` | `(current, pending)` timeline pair; mirrors `nous.link`; fires pending on `apply_at` |
| Modulator engine | _(private)_ | Runs autonomous RT modulators at event-loop block rate; routes normalised output to registered consumers |
| Slope modulator | _(private)_ | Tides-inspired LFO; rate/shape/slope/smoothness/depth/bipolar params |
| Segment modulator | _(private)_ | Stages-inspired multi-segment function generator; up to 36 segments; free-running via `loop=true` |
| SPSC queue | `nomos/rt/spsc_queue.hpp` | Lock-free single-producer/single-consumer ring buffer; power-of-two capacity |
| MIDI I/O | _(private)_ | RtMidi wrapper; `open-input!` / `open-output!`; push-handler model |
| OSC server | _(private)_ | UDP receive thread; handler dispatch |
| Audio device | _(private)_ | RtAudio wrapper; block-rate callback |
| Link peer | _(private)_ | Ableton Link integration; beat/phase/BPM; `start`/`stop` transport |
| Event scheduler | `nomos/rt/event_scheduler.hpp` | Beat-accurate event dispatch |
| RCU | `nomos/rt/rcu.hpp` | Userspace RCU (urcu-bp) for lock-free hot-swap |

## IPC protocol

All messages share an 8-byte header:

```
[uint32_t payload_len][uint8_t type][uint8_t reserved[3]]
```

`payload_len` is big-endian; callers byte-swap on little-endian hosts.

Common message types (shared by kairos and aion):

| Code | Name | Payload |
|------|------|---------|
| `0x30` | `MSG-TX-LOG` | EDN keyword source id + tx entry |
| `0x31` | `MSG-SESSION-OPEN` | EDN session metadata |
| `0x32` | `MSG-SESSION-CLOSE` | — |
| `0x33` | `MSG-REGISTER-SOURCE` | EDN `{:id :kw :name "..." :description "..."}` |
| `0x34` | `MSG-GRAPH-LOAD` | EDN plugin graph description |
| `0x35` | `MSG-GRAPH-RESET` | — |
| `0x40` | `MSG-PARAM-SET` | EDN path + `(current, pending)` tuple |
| `0x41` | `MSG-NOTE-ON` | EDN note-on event |
| `0x42` | `MSG-NOTE-OFF` | EDN note-off event |
| `0x43` | `MSG-MIDI-IN` | EDN raw MIDI bytes |
| `0x44` | `MSG-WASM-HOT-SWAP` | EDN `{:node-id :kw :wasm-path "..."}` |
| `0x45` | `MSG-SCHEDULE-BUNDLE` | EDN `{:at-beat D :events [...]}` |
| `0x46` | `MSG-MODULATOR-START` | EDN `{:id :kw :type :slope\|:segment :rate 1.0 ...}` |
| `0x47` | `MSG-MODULATOR-STOP` | EDN `{:id :kw}` |
| `0x48` | `MSG-MODULATOR-UPDATE` | EDN `{:id :kw :key "rate" :value 0.5}` |
| `0x50` | `MSG-TICK` | EDN `{:beat D :tick-n N}` pushed on each 24 PPQN tick |

## Time identity

The canonical session timing state mirrors `nous.link`:

```cpp
struct timeline { double bpm; double beat; };

struct pending_transition {
    timeline     tl;
    edn::keyword policy;   // :bar-quantize | :snap | :smooth
    double       apply_at; // resolved beat — authoritative
};

struct time_identity {
    timeline                          current;
    std::optional<pending_transition> pending;

    bool apply_if_ready(double current_beat) noexcept;
};
```

`apply_if_ready` promotes `pending → current` when `current_beat >= apply_at`. Call from the audio/process thread each block.

## Modulators

RT modulators run autonomously at event-loop block rate — no per-tick IPC.

```cpp
// Start a slope (Tides-inspired) LFO via the engine:
engine.start("filter-lfo", std::make_unique<slope_modulator>());
engine.update_param("filter-lfo", "rate", 0.5f);    // 0.5 Hz
engine.update_param("filter-lfo", "depth", 0.8f);

// Each block:
engine.tick(current_beat, tick_rate_hz, [&](const std::string& id, float v) {
    // v is normalised: [-1, 1] bipolar or [0, 1] unipolar
    route_to_param(id, v);
});
```

Slope modulator parameters: `rate` (Hz, 0.001–100), `shape` [-1..1], `slope` [-1..1], `smoothness` [-1..1], `depth` [0..1], `bipolar` (1.0 = bipolar).

Segment modulator: up to 36 segments, each with `type` (`:ramp`/`:step`/`:hold`/`:alt`), `primary` [0..1], `secondary` [0..1], `loop` bool. Parameters updated via `"segment_N_primary"` / `"segment_N_secondary"` keys.

## Consuming nomos-rt

Fetch via CMake FetchContent:

```cmake
FetchContent_Declare(nomos-rt
    GIT_REPOSITORY https://github.com/nomos-studio/nomos-rt.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE)
set(NOMOS_RT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nomos-rt)

# Public API (IPC types, time_identity, spsc_queue, session, etc.):
target_link_libraries(my-exe PRIVATE nomos::rt)

# Also link this to access private implementation headers (link_peer, midi_io,
# osc_server, modulator types) — intended for thin executables like kairos/aion:
target_link_libraries(my-exe PRIVATE nomos::rt-exe-headers)
```

For local development, override the txlog and edn-cpp fetches:

```sh
cmake --preset dev \
  -DTXLOG_CPP_DIR=/path/to/txlog/cpp \
  -DEDN_CPP_DIR=/path/to/edn-cpp
```

## Building and testing

```sh
cmake --preset dev      # tests on, Debug, compile_commands.json
cmake --build --preset dev
ctest --preset dev
```

39 tests; covers modulator engine, slope/segment modulators, spsc_queue, and time_identity.

## Dependencies

| Dependency | How fetched | Licence |
|------------|-------------|---------|
| [txlog](https://github.com/nomos-studio/txlog) | FetchContent (`SOURCE_SUBDIR cpp`) | LGPL-2.1-or-later |
| [edn-cpp](https://github.com/nomos-studio/edn-cpp) | via txlog | MIT |
| [CLAP SDK](https://github.com/free-audio/clap) | FetchContent | MIT |
| [RtMidi](https://github.com/thestk/rtmidi) | FetchContent | MIT |
| [RtAudio](https://github.com/thestk/rtaudio) | FetchContent | MIT |
| [Ableton Link](https://github.com/Ableton/link) | FetchContent | GPL-2.0-or-later |
| liburcu (urcu-bp) | vendored in `third_party/` | LGPL-2.1-or-later |
| Mutable Instruments Tides / Stages | vendored in `vendor/mi/` | MIT |
| [Catch2](https://github.com/catchorg/Catch2) | FetchContent (tests only) | BSL-1.0 |

SQLite3 is required as a system package (`find_package(SQLite3 REQUIRED)`).

**Licence note:** Ableton Link is GPL-2.0-or-later. nomos-rt is LGPL-2.1-or-later at the library level, but any executable that links `nomos::rt` is GPL-2.0-or-later at the binary level due to the Link dependency.

## Licence

LGPL-2.1-or-later. See [`LICENSES/`](LICENSES/) for full texts.
