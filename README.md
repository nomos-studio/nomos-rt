# nomos-rt

Shared C++ runtime substrate for the [nomos-studio](https://github.com/nomos-studio) ecosystem. Provides the common foundation that [kairos](https://github.com/nomos-studio/kairos) (CLAP container) and [aion](https://github.com/nomos-studio/aion) (session peer) are built on.

## What's in here

| Component | Headers | Description |
|-----------|---------|-------------|
| IPC channel | `nomos/rt/ipc.hpp`, `ipc_channel.hpp` | Unix domain socket framing; 8-byte header + EDN payload |
| Session / txlog | `nomos/rt/session.hpp` | Opens a txlog SQLite database; maps SESSION-OPEN/CLOSE/REGISTER-SOURCE |
| Control thread base | `nomos/rt/rt_control_thread.hpp` | Listens on the IPC socket; dispatches common message types; virtual extension point for CLAP/aion-specific messages |
| Time identity | `nomos/rt/time_identity.hpp` | `(current, pending)` timeline pair; mirrors `nous.link`; fires pending on `apply_at` |
| Abstract modulator | `nomos/rt/abstract_modulator.hpp` | Pure virtual interface (`tick` → `modulator_output`, `update`); base for all built-in and custom modulators |
| Modulator engine | `nomos/rt/modulator_engine.hpp` | Runs autonomous RT modulators at event-loop block rate; RCU-protected table; zero-allocation hot path |
| Modulator registry | `nomos/rt/modulator_registry.hpp` | Maps type-name strings to user-defined modulator factories; plug into `rt_control_thread::config::ext_registry` |
| Slope modulator | _(private)_ | First-principles LFO; rate/shape/slope/smoothness/depth/bipolar params |
| Segment modulator | _(private)_ | Multi-segment function generator; up to 36 segments; free-running via `loop=true` |
| Slew modulator | _(private)_ | Slew-rate limiter / lag processor; `rise` and `fall` time params |
| Shift-register modulator | _(private)_ | Looping shift register with probability; voltage-controlled pseudo-randomness |
| Fractal modulator | _(private)_ | Recursive subdivision curve; `depth` and `roughness` params |
| Stochastic modulator | _(private)_ | Sample-and-hold with configurable probability distribution |
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
| `0x46` | `MSG-MODULATOR-START` | EDN `{:id :kw :type :slope\|:segment\|:slew\|... :rate 1.0 ...}` |
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

RT modulators run autonomously at event-loop block rate — no per-tick IPC round-trip.

### Modulator output

All modulators return a `modulator_output` struct:

```cpp
struct modulator_output {
    float    cv{0.0f};    // primary CV: normalised [-1,1] bipolar or [0,1] unipolar
    float    aux{0.0f};   // secondary output (phase, ramp, etc.)
    bool     gate{false}; // gate high on cycle reset / trigger events
    bool     gate2{false};// secondary gate (e.g. end-of-cycle for segment modulators)
    uint32_t state{0};    // modulator-specific state word
};
```

### Using the engine

```cpp
#include <nomos/rt/modulator_engine.hpp>

// Start modulators (built-in types are constructed via IPC from nous;
// the engine can also be driven directly in tests or embedded contexts):
engine.start("filter-lfo", std::make_unique<slope_modulator>());
engine.update_param("filter-lfo", "rate", 0.5f);    // 0.5 Hz
engine.update_param("filter-lfo", "depth", 0.8f);

// Each block — zero std::function overhead; Fn is a template parameter:
engine.tick(current_beat, tick_rate_hz,
    [&](const std::string& id, const nomos::rt::modulator_output& out) {
        route_cv(id, out.cv);
        if (out.gate) trigger_env(id);
    });
```

In kairos the tick callback writes directly into the CLAP `clap_event_param_value` queue, giving sample-accurate parameter animation with no scheduler jitter.

### Built-in modulator parameters

**Slope** (`type: :slope`): `rate` (Hz, 0.001–100), `shape` [-1..1], `slope` [-1..1], `smoothness` [-1..1], `depth` [0..1], `bipolar` (1.0 = bipolar output).

**Segment** (`type: :segment`): up to 36 segments, each with `type` (`:ramp`/`:step`/`:hold`/`:alt`), `primary` [0..1], `secondary` [0..1], `loop` bool. Updated via `"segment_N_primary"` / `"segment_N_secondary"` keys.

**Slew** (`type: :slew`): `rise` (seconds, 0..10), `fall` (seconds, 0..10), `target` [0..1].

**Shift register** (`type: :shift-register`): `length` (int, 1–32), `probability` [0..1], `seed` (float, used as initial value).

**Fractal** (`type: :fractal`): `depth` (int, 1–8), `roughness` [0..1], `rate` (Hz).

**Stochastic** (`type: :stochastic`): `rate` (Hz), `probability` [0..1], `min` [0..1], `max` [0..1].

### Custom modulator types

Implement `abstract_modulator` and register a factory:

```cpp
#include <nomos/rt/abstract_modulator.hpp>
#include <nomos/rt/modulator_registry.hpp>

class my_modulator final : public nomos::rt::abstract_modulator {
public:
    nomos::rt::modulator_output tick(double beat, float tick_rate_hz) override {
        // ... compute output ...
        return {.cv = value_};
    }
    void update(std::string_view key, float value) override {
        if (key == "speed") speed_ = value;
    }
private:
    float speed_{1.0f};
    float value_{0.0f};
};

// Register so MSG-MODULATOR-START {:type :my-modulator ...} works from nous:
nomos::rt::modulator_registry registry;
registry.register_type("my-modulator", []() {
    return std::make_unique<my_modulator>();
});

// Pass into rt_control_thread:
nomos::rt::rt_control_thread::config cfg;
cfg.socket_path  = "/tmp/nomos.sock";
cfg.mod_engine   = &engine;
cfg.ext_registry = &registry;
```

Float-typed EDN parameters in the `:start` message are automatically applied via `update()` after construction.

## Consuming nomos-rt

Fetch via CMake FetchContent:

```cmake
FetchContent_Declare(nomos-rt
    GIT_REPOSITORY https://github.com/nomos-studio/nomos-rt.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE)
set(NOMOS_RT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nomos-rt)

# Public API — IPC types, time_identity, spsc_queue, session, modulator interface:
target_link_libraries(my-exe PRIVATE nomos::rt)

# Also link this to access private implementation headers (concrete modulator types,
# link_peer, midi_io, osc_server) — intended for thin executables like kairos/aion:
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

122 tests; covers all six modulator types, modulator engine, spsc_queue, and time_identity.

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
| [Catch2](https://github.com/catchorg/Catch2) | FetchContent (tests only) | BSL-1.0 |

SQLite3 is required as a system package (`find_package(SQLite3 REQUIRED)`).

**Licence note:** Ableton Link is GPL-2.0-or-later. nomos-rt is LGPL-2.1-or-later at the library level, but any executable that links `nomos::rt` is GPL-2.0-or-later at the binary level due to the Link dependency.

## Licence

LGPL-2.1-or-later. See [`LICENSES/`](LICENSES/) for full texts.
