// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// luajit_participant — optional LuaJIT + Fennel scripting layer.
//
// Constructed via load().  Returns nullptr when LuaJIT or Fennel is not
// available.  All Lua calls are serialised by an internal mutex so the
// caller (rt_control_thread) can invoke eval() safely from the IPC thread
// while the timer thread fires tick() concurrently.
//
// Lifecycle:
//   auto lua = luajit_participant::load();
//   if (lua) cfg.lua = lua.get();
//   // ... run ...
//   lua.reset(); // stops timer thread, closes Lua state

struct lua_State; // forward — avoid pulling in lua.h here

namespace nomos::rt {

class luajit_participant {
  public:
    // Load LuaJIT and Fennel.  fennel_path overrides the default search order
    // (see cpp for the list).  Returns nullptr if either is unavailable.
    static std::unique_ptr<luajit_participant> load(std::string_view fennel_path = {});

    ~luajit_participant();

    luajit_participant(const luajit_participant&)            = delete;
    luajit_participant& operator=(const luajit_participant&) = delete;

    // Evaluate a Fennel expression.  Returns an EDN-formatted value string,
    // e.g. "3" for (+ 1 2), or "{:error \"…\"}" on Fennel error.
    std::string eval(std::string_view src);

    // Update beat/BPM state (called from Link peer or heartbeat).
    // Thread-safe — atomic store.
    void set_beat(double beat, float bpm_hz) noexcept;

    // Register the VCVRack WASM hot-swap C binding.
    // After this call, Fennel code may invoke (nomos_vcv_hot_swap slot wasm-path).
    // Thread-safe — takes the Lua mutex internally.
    void register_wasm_swap_fn(
        std::function<bool(std::string_view node_id, std::string_view wasm_path)> fn);

    // Dispatch an incoming OSC message to a registered Fennel handler.
    // type_tags must NOT include the leading ','.  args/args_len are the raw
    // OSC argument bytes.  Returns true if a handler was found and called
    // (even if the handler itself raised a Fennel error).
    // Thread-safe — takes the Lua mutex internally.
    bool dispatch_osc(std::string_view path, std::string_view type_tags, const uint8_t* args,
                      std::size_t args_len);

    // Return the keys of nomos-osc-dispatch as a newline-delimited string.
    // Used by osc_server to respond to /nous/endpoints pull requests.
    // Thread-safe — takes the Lua mutex internally.
    std::string get_osc_endpoints();

    // Register the OSC push-endpoints C binding.
    // After this call, (osc.register ...) will push /nous/endpoints to the
    // last known OSC sender via the osc_server::push_endpoints() lambda.
    // Thread-safe — takes the Lua mutex internally.
    void register_osc_push_fn(std::function<void()> fn);

  private:
    explicit luajit_participant(lua_State* L);

    // Called from the 25 Hz timer thread.
    void tick_once();

    // Evaluate the REPL prelude — defines schedule-at, ctrl, fire-and-forget etc.
    // Called once during load() before the tick thread starts.
    void load_prelude();

    lua_State* L_{nullptr};
    std::mutex mutex_;

    // Beat position shared with timer thread via atomics.
    std::atomic<double> beat_{0.0};
    std::atomic<float>  bpm_{120.f};

    std::thread       tick_thread_;
    std::atomic<bool> tick_running_{false};

    // Heap-allocated callables registered as Lua C function upvalues.
    // Must outlive the Lua state — destroyed in ~luajit_participant after lua_close.
    using WasmSwapFn = std::function<bool(std::string_view, std::string_view)>;
    using OscPushFn  = std::function<void()>;
    std::vector<std::unique_ptr<WasmSwapFn>> held_wasm_fns_;
    std::vector<std::unique_ptr<OscPushFn>>  held_osc_push_fns_;
};

} // namespace nomos::rt
