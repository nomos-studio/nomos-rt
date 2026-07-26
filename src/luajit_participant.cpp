// SPDX-License-Identifier: GPL-2.0-or-later
#include "luajit_participant.hpp"

#ifdef NOMOS_RT_HAS_LUAJIT

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

// Candidate paths for fennel.lua, tried in order.
const char* const kFennelPaths[] = {
    "/opt/homebrew/share/lua/5.1/fennel.lua", // macOS arm64 Homebrew
    "/usr/local/share/lua/5.1/fennel.lua",    // macOS x86 Homebrew / manual
    "/usr/share/lua/5.1/fennel.lua",          // Linux system
    "fennel.lua",                             // cwd fallback
    nullptr,
};

std::string slurp(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// lua_tostring is a macro in Lua 5.1 — use lua_tolstring directly.
std::string lua_tostring_safe(lua_State* L, int idx) {
    std::size_t len = 0;
    const char* s   = lua_tolstring(L, idx, &len);
    if (!s)
        return {};
    return {s, len};
}

// Serialize the top-of-stack Lua value to an EDN string.
// Numbers → integer or float literal; strings → quoted; bool/nil → keyword.
// Leaves the stack unchanged.
std::string lua_to_edn(lua_State* L, int idx) {
    const int t = lua_type(L, idx);
    switch (t) {
    case LUA_TNUMBER: {
        const double      n = lua_tonumber(L, idx);
        const lua_Integer i = lua_tointeger(L, idx);
        if (static_cast<double>(i) == n)
            return std::to_string(i);
        // Format as float; strip trailing zeros.
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%g", n);
        return buf;
    }
    case LUA_TBOOLEAN:
        return lua_toboolean(L, idx) ? "true" : "false";
    case LUA_TNIL:
        return "nil";
    case LUA_TSTRING: {
        const std::string raw = lua_tostring_safe(L, idx);
        // Simple EDN string escaping: backslash and double-quote.
        std::string out;
        out.reserve(raw.size() + 2);
        out += '"';
        for (char c : raw) {
            if (c == '"' || c == '\\')
                out += '\\';
            out += c;
        }
        out += '"';
        return out;
    }
    default: {
        // Coerce via tostring().
        lua_getglobal(L, "tostring");
        lua_pushvalue(L, idx);
        if (lua_pcall(L, 1, 1, 0) == 0) {
            std::string s = "\"" + lua_tostring_safe(L, -1) + "\"";
            lua_pop(L, 1);
            return s;
        }
        lua_pop(L, 1);
        return ":unknown";
    }
    }
}

// OSC helpers — same encoding as osc_server.cpp but duplicated here to avoid
// a header dependency between the two translation units.
inline int32_t osc_read_i32(const uint8_t* p) noexcept {
    uint32_t v;
    std::memcpy(&v, p, 4);
    v = ((v & 0xFF000000u) >> 24) | ((v & 0x00FF0000u) >> 8) | ((v & 0x0000FF00u) << 8) |
        ((v & 0x000000FFu) << 24);
    return static_cast<int32_t>(v);
}
inline float osc_read_f32(const uint8_t* p) noexcept {
    uint32_t v;
    std::memcpy(&v, p, 4);
    v = ((v & 0xFF000000u) >> 24) | ((v & 0x00FF0000u) >> 8) | ((v & 0x0000FF00u) << 8) |
        ((v & 0x000000FFu) << 24);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}
inline std::size_t osc_pad4(std::size_t n) noexcept {
    return (n + 3) & ~std::size_t{3};
}

// Lua C function for osc.register push-endpoints:  (nomos_osc_push_endpoints)
// Upvalue 1: OscPushFn* — calls osc_server::push_endpoints().
static int l_osc_push_endpoints(lua_State* L) {
    using Fn = std::function<void()>;
    auto* fn = static_cast<Fn*>(lua_touserdata(L, lua_upvalueindex(1)));
    if (fn)
        (*fn)();
    lua_pushboolean(L, 1);
    return 1;
}

// Lua C function for vcv.hot-swap!:  (nomos_vcv_hot_swap slot wasm-path)
// Upvalue 1 is a WasmSwapFn* stored as light userdata.
// arg1 = slot string (e.g. "0"), arg2 = wasm path string.
// Returns an EDN status map.
static int l_vcv_hot_swap(lua_State* L) {
    using Fn         = std::function<bool(std::string_view, std::string_view)>;
    auto*       fn   = static_cast<Fn*>(lua_touserdata(L, lua_upvalueindex(1)));
    std::size_t len1 = 0, len2 = 0;
    const char* s1 = lua_tolstring(L, 1, &len1);
    const char* s2 = lua_tolstring(L, 2, &len2);
    if (!fn || !s1 || !s2) {
        lua_pushstring(L, "{:status :error :reason \"bad args\"}");
        return 1;
    }
    // Construct node_id as "vcv-slot-N" from the slot string argument.
    const std::string node_id = std::string("vcv-slot-") + std::string(s1, len1);
    const bool        ok      = (*fn)(node_id, std::string_view(s2, len2));
    lua_pushstring(L, ok ? "{:status :ok}" : "{:status :error :reason \"swap failed\"}");
    return 1;
}

} // namespace

namespace nomos::rt {

// ---- Construction -----------------------------------------------------------

luajit_participant::luajit_participant(lua_State* L) : L_(L) {
    tick_running_.store(true, std::memory_order_relaxed);
    tick_thread_ = std::thread([this] {
        while (tick_running_.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(40)); // ~25 Hz
            tick_once();
        }
    });
}

luajit_participant::~luajit_participant() {
    tick_running_.store(false, std::memory_order_relaxed);
    if (tick_thread_.joinable())
        tick_thread_.join();
    if (L_) {
        lua_close(L_);
        L_ = nullptr;
    }
}

// ---- Factory ----------------------------------------------------------------

std::unique_ptr<luajit_participant> luajit_participant::load(std::string_view fennel_path_hint) {
    // Locate fennel.lua.
    std::string fennel_src;
    if (!fennel_path_hint.empty()) {
        fennel_src = slurp(std::string(fennel_path_hint).c_str());
    }
    if (fennel_src.empty()) {
        for (const char* const* p = kFennelPaths; *p; ++p) {
            fennel_src = slurp(*p);
            if (!fennel_src.empty()) {
                std::fprintf(stderr, "[nomos-rt] LuaJIT participant: loaded Fennel from %s\n", *p);
                break;
            }
        }
    }
    if (fennel_src.empty()) {
        std::fprintf(
            stderr,
            "[nomos-rt] LuaJIT participant: fennel.lua not found — Fennel eval unavailable\n");
        return nullptr;
    }

    // Create Lua state and open standard libraries.
    lua_State* L = luaL_newstate();
    if (!L) {
        std::fprintf(stderr, "[nomos-rt] LuaJIT participant: luaL_newstate() failed\n");
        return nullptr;
    }
    luaL_openlibs(L);

    // Load Fennel into a global named "fennel".
    if (luaL_loadbuffer(L, fennel_src.c_str(), fennel_src.size(), "fennel") != 0) {
        std::fprintf(stderr, "[nomos-rt] LuaJIT participant: Fennel load error: %s\n",
                     lua_tostring_safe(L, -1).c_str());
        lua_close(L);
        return nullptr;
    }
    if (lua_pcall(L, 0, 1, 0) != 0) {
        std::fprintf(stderr, "[nomos-rt] LuaJIT participant: Fennel init error: %s\n",
                     lua_tostring_safe(L, -1).c_str());
        lua_close(L);
        return nullptr;
    }
    lua_setglobal(L, "fennel"); // pops the Fennel module table

    auto participant = std::unique_ptr<luajit_participant>(new luajit_participant(L));
    participant->load_prelude();
    std::fprintf(stderr, "[nomos-rt] LuaJIT participant loaded\n");
    return participant;
}

// ---- eval -------------------------------------------------------------------

std::string luajit_participant::eval(std::string_view src) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (!L_)
        return "{:error \"Lua state closed\"}";

    const int base = lua_gettop(L_);

    // Push fennel.eval and call it with the source string.
    lua_getglobal(L_, "fennel");
    if (lua_type(L_, -1) != LUA_TTABLE) {
        lua_settop(L_, base);
        return "{:error \"fennel global missing\"}";
    }
    lua_getfield(L_, -1, "eval");
    if (lua_type(L_, -1) != LUA_TFUNCTION) {
        lua_settop(L_, base);
        return "{:error \"fennel.eval missing\"}";
    }
    lua_pushlstring(L_, src.data(), src.size());

    if (lua_pcall(L_, 1, 1, 0) != 0) {
        const std::string err = lua_tostring_safe(L_, -1);
        lua_settop(L_, base);
        // Escape quotes in the error message for the EDN string.
        std::string escaped;
        for (char c : err) {
            if (c == '"' || c == '\\')
                escaped += '\\';
            escaped += c;
        }
        return "{:error \"" + escaped + "\"}";
    }

    std::string result = lua_to_edn(L_, -1);
    lua_settop(L_, base);
    return result;
}

// ---- REPL prelude -----------------------------------------------------------
//
// Fennel source evaluated once at startup.  Defines the vocabulary available
// in all subsequent fennel.eval() calls:
//
//   schedule-at [when thunk]   — fire thunk at next :beat (1 beat) or :bar (4 beats)
//   fire-and-forget [thunk]    — call thunk immediately; return "ok"/error string
//   ctrl                       — table: ctrl.set! [path val], ctrl.get [path]
//   nomos_rt_tick [beat bpm]   — tick callback; overrides the C-checked global;
//                                updates nomos-beat / nomos-bpm globals and drains
//                                the scheduled event queue

static const char kPrelude[] = R"FNLEOF(
;; nomos REPL prelude — auto-loaded at LuaJIT startup.
;; All (global ...) bindings persist across fennel.eval() calls in this Lua state.

;; Beat clock state — written each tick, read by user code.
(global nomos-beat 0.0)
(global nomos-bpm  120.0)

;; Scheduled event queue: [{:target <beat> :thunk <fn>} ...]
(global nomos-scheduled [])

;; schedule-at [timing thunk]
;; timing  :beat → next beat boundary (current + 1)
;;         :bar  → next bar  boundary (current + 4, assuming 4/4)
;; Returns a status string displayed on the TTY screen.
(global schedule-at
  (fn [timing thunk]
    (let [step   (if (= timing :beat) 1 4)
          target (+ nomos-beat step)]
      (table.insert nomos-scheduled {:target target :thunk thunk})
      (string.format "scheduled %.2f (%s)" target (tostring timing)))))

;; fire-and-forget [thunk] — execute thunk immediately; surface errors as strings.
(global fire-and-forget
  (fn [thunk]
    (let [(ok err) (pcall thunk)]
      (if ok "ok" (.. "! " (tostring err))))))

;; ctrl — ctrl-tree namespace.  Stubs return informative strings until
;; protomatter/ctrl-tree C bindings are wired in.
(global ctrl
  {:set! (fn [path val]
           (let [f (. _G :nomos_ctrl_set)]
             (if f
               (f path val)
               (.. "ctrl.set! " (tostring path) " " (tostring val)))))
   :get  (fn [path]
           (let [f (. _G :nomos_ctrl_get)]
             (if f
               (f path)
               (.. "ctrl.get " (tostring path) " (stub)"))))})

;; vcv — VCVRack bridge namespace.
;;   vcv.hot-swap! [slot wasm-path]  — load a pre-compiled WASM into a bridge slot.
;;     slot      — integer bridge slot index (0-based).
;;     wasm-path — absolute path to a Faust/Alembic-compiled .wasm file.
;;     Calls the nomos_vcv_hot_swap C binding (registered by rt_control_thread::start).
;;     Falls back to an informative string when the binding is not yet wired.
;;
;;   vcv.load-alembic! [slot expr] — compile an Alembic expression and hot-swap it.
;;     This stub surfaces the deux façade: full pipeline goes via nous (#alembic prefix
;;     from the TTY REPL routes to :nous which sends msg_wasm_hot_swap on completion).
(global vcv
  {:hot-swap!
   (fn [slot wasm-path]
     (let [f (. _G :nomos_vcv_hot_swap)]
       (if f
         (f (tostring slot) (tostring wasm-path))
         (.. "vcv.hot-swap! not wired"
             " (slot=" (tostring slot)
             " path=" (tostring wasm-path) ")"))))
   :load-alembic!
   (fn [slot expr]
     ;; Full pipeline: type '#alembic N expr' at TTY → nous compiles → WASM →
     ;; msg_wasm_hot_swap (0x44) → vcv.hot-swap! (wired when Phase 10 is live).
     (.. "(alembic/compile-and-load! " (tostring slot)
         " '" (tostring expr) ")"))})

;; osc — OSC endpoint registry.
;;
;;   (osc.register "/grid/press" (fn [x y state] ...))
;;     Registers a handler for an OSC address.  The handler is called with
;;     OSC arguments decoded as Lua values (i→integer, f→number, s→string).
;;     After registration, nomos_osc_push_endpoints fires so that connected
;;     controllers receive a /nous/endpoints update automatically.
;;
;;   (osc.unregister "/grid/press")
;;     Removes a handler and notifies connected controllers.
(global nomos-osc-dispatch {})

(global osc
  {:register
   (fn [path handler]
     (tset nomos-osc-dispatch path handler)
     (let [f (. _G :nomos_osc_push_endpoints)]
       (when f (f)))
     (.. "osc: registered " path))
   :unregister
   (fn [path]
     (tset nomos-osc-dispatch path nil)
     (let [f (. _G :nomos_osc_push_endpoints)]
       (when f (f)))
     (.. "osc: unregistered " path))})

;; nomos_rt_tick [beat bpm] — called at ~25 Hz by the C timer thread.
;; Updates beat globals and fires any scheduled events whose target has passed.
(global nomos_rt_tick
  (fn [beat bpm]
    (set nomos-beat beat)
    (set nomos-bpm  bpm)
    (var i 1)
    (while (<= i (length nomos-scheduled))
      (let [ev (. nomos-scheduled i)]
        (if (>= beat ev.target)
          (do
            (table.remove nomos-scheduled i)
            (pcall ev.thunk))
          (set i (+ i 1)))))))
)FNLEOF";

void luajit_participant::register_wasm_swap_fn(
    std::function<bool(std::string_view, std::string_view)> fn) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (!L_)
        return;
    held_wasm_fns_.push_back(std::make_unique<WasmSwapFn>(std::move(fn)));
    lua_pushlightuserdata(L_, held_wasm_fns_.back().get());
    lua_pushcclosure(L_, l_vcv_hot_swap, 1);
    lua_setglobal(L_, "nomos_vcv_hot_swap");
}

void luajit_participant::register_osc_push_fn(std::function<void()> fn) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (!L_)
        return;
    held_osc_push_fns_.push_back(std::make_unique<OscPushFn>(std::move(fn)));
    lua_pushlightuserdata(L_, held_osc_push_fns_.back().get());
    lua_pushcclosure(L_, l_osc_push_endpoints, 1);
    lua_setglobal(L_, "nomos_osc_push_endpoints");
}

bool luajit_participant::dispatch_osc(std::string_view path, std::string_view type_tags,
                                      const uint8_t* args, std::size_t args_len) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (!L_)
        return false;
    const int base = lua_gettop(L_);

    // Look up the handler in the nomos-osc-dispatch table.
    lua_getglobal(L_, "nomos-osc-dispatch");
    if (lua_type(L_, -1) != LUA_TTABLE) {
        lua_settop(L_, base);
        return false;
    }
    lua_pushlstring(L_, path.data(), path.size());
    lua_gettable(L_, -2); // dispatch[path]
    if (lua_type(L_, -1) != LUA_TFUNCTION) {
        lua_settop(L_, base);
        return false;
    }

    // Push each OSC argument as the corresponding Lua type.
    std::size_t arg_off = 0;
    int         nargs   = 0;
    for (char tag : type_tags) {
        if (tag == 'i' && arg_off + 4 <= args_len) {
            lua_pushinteger(L_, static_cast<lua_Integer>(osc_read_i32(args + arg_off)));
            arg_off += 4;
            ++nargs;
        } else if (tag == 'f' && arg_off + 4 <= args_len) {
            lua_pushnumber(L_, static_cast<lua_Number>(osc_read_f32(args + arg_off)));
            arg_off += 4;
            ++nargs;
        } else if (tag == 's' && arg_off < args_len) {
            const char*       s    = reinterpret_cast<const char*>(args + arg_off);
            const std::size_t slen = ::strnlen(s, args_len - arg_off);
            lua_pushlstring(L_, s, slen);
            arg_off += osc_pad4(slen + 1);
            ++nargs;
        }
        // blob, timetag etc. — not used in the controller profiles we target
    }

    if (lua_pcall(L_, nargs, 0, 0) != 0) {
        std::fprintf(stderr, "[nomos-rt] osc handler error [%.*s]: %s\n",
                     static_cast<int>(path.size()), path.data(), lua_tostring_safe(L_, -1).c_str());
    }
    lua_settop(L_, base);
    return true;
}

std::string luajit_participant::get_osc_endpoints() {
    std::lock_guard<std::mutex> lk(mutex_);
    if (!L_)
        return {};
    const int base = lua_gettop(L_);

    lua_getglobal(L_, "nomos-osc-dispatch");
    if (lua_type(L_, -1) != LUA_TTABLE) {
        lua_settop(L_, base);
        return {};
    }

    std::string result;
    lua_pushnil(L_); // first key for lua_next
    while (lua_next(L_, -2) != 0) {
        if (lua_type(L_, -2) == LUA_TSTRING)
            result += lua_tostring_safe(L_, -2) + '\n';
        lua_pop(L_, 1); // pop value, keep key for next iteration
    }
    lua_settop(L_, base);
    return result;
}

void luajit_participant::load_prelude() {
    // load_prelude() is called during load() before the tick thread starts,
    // so no mutex is needed here.
    const int base = lua_gettop(L_);

    lua_getglobal(L_, "fennel");
    if (lua_type(L_, -1) != LUA_TTABLE) {
        std::fprintf(stderr, "[nomos-rt] load_prelude: fennel global missing\n");
        lua_settop(L_, base);
        return;
    }
    lua_getfield(L_, -1, "eval");
    if (lua_type(L_, -1) != LUA_TFUNCTION) {
        std::fprintf(stderr, "[nomos-rt] load_prelude: fennel.eval missing\n");
        lua_settop(L_, base);
        return;
    }
    lua_pushlstring(L_, kPrelude, sizeof(kPrelude) - 1);

    if (lua_pcall(L_, 1, 1, 0) != 0) {
        std::fprintf(stderr, "[nomos-rt] REPL prelude error: %s\n",
                     lua_tostring_safe(L_, -1).c_str());
        lua_settop(L_, base);
        return;
    }

    lua_settop(L_, base);
    std::fprintf(stderr, "[nomos-rt] REPL prelude loaded (schedule-at / ctrl / fire-and-forget)\n");
}

// ---- beat state -------------------------------------------------------------

void luajit_participant::set_beat(double beat, float bpm_hz) noexcept {
    beat_.store(beat, std::memory_order_relaxed);
    bpm_.store(bpm_hz, std::memory_order_relaxed);
}

// ---- timer tick -------------------------------------------------------------

void luajit_participant::tick_once() {
    std::lock_guard<std::mutex> lk(mutex_);
    if (!L_)
        return;

    lua_getglobal(L_, "nomos_rt_tick");
    if (lua_type(L_, -1) != LUA_TFUNCTION) {
        lua_pop(L_, 1);
        return;
    }
    lua_pushnumber(L_, beat_.load(std::memory_order_relaxed));
    lua_pushnumber(L_, static_cast<double>(bpm_.load(std::memory_order_relaxed)));
    // Ignore result and errors from the tick function.
    if (lua_pcall(L_, 2, 0, 0) != 0)
        lua_pop(L_, 1);
}

} // namespace nomos::rt

#else // NOMOS_RT_HAS_LUAJIT

// Stub implementation when LuaJIT was not found at build time.
#include <cstdio>

namespace nomos::rt {

std::unique_ptr<luajit_participant> luajit_participant::load(std::string_view) {
    std::fprintf(stderr, "[nomos-rt] LuaJIT support not compiled in (rebuild with LuaJIT)\n");
    return nullptr;
}

luajit_participant::~luajit_participant() = default;
std::string luajit_participant::eval(std::string_view) {
    return "{:error \"LuaJIT not compiled in\"}";
}
void luajit_participant::set_beat(double, float) noexcept {
}
void luajit_participant::tick_once() {
}

} // namespace nomos::rt

#endif // NOMOS_RT_HAS_LUAJIT
