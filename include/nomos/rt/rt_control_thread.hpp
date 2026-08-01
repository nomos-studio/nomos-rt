// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <nomos/rt/input_event.hpp>
#include <nomos/rt/ipc_channel.hpp>
#include <nomos/rt/param_event.hpp>
#include <nomos/rt/scheduled_event.hpp>
#include <nomos/rt/session.hpp>
#include <nomos/rt/spsc_queue.hpp>

// Forward declarations — avoid pulling internal headers into consumers.
namespace nomos::rt {
class luajit_participant;
class modulator_engine;
class modulator_registry;
class midi_io;    // midi_io.hpp is internal (GPL); consumers use a pointer
class osc_server; // osc_server.hpp is internal (GPL); consumers use a pointer
} // namespace nomos::rt

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace nomos::rt {

// Capacity of the param event queue shared with the audio thread.
// Must be a power of two.  512 slots is generous for block-rate param updates.
constexpr std::size_t param_queue_capacity = 512;

using param_queue = spsc_queue<param_event, param_queue_capacity>;

// Non-CLAP foundation of the kairos control thread.
//
// Listens on a Unix domain socket, accepts one connection at a time, and
// dispatches on the common nomos-rt message types:
//   SESSION-OPEN / SESSION-CLOSE / REGISTER-SOURCE
//   TX-LOG / PARAM-SET
//   NOTE-ON / NOTE-OFF / MIDI-IN
//
// CLAP-specific or runtime-specific messages are forwarded to
// dispatch_extension(), which derived classes override.
//
// The caller owns the param_queue and passes a reference; the audio / process
// thread consumes from the same queue.
class rt_control_thread {
  public:
    struct config {
        std::string          socket_path;            // Unix domain socket path
        std::string          db_path;                // txlog database path
        sched_staging_queue* sched_staging{nullptr}; // null = immediate dispatch
        modulator_engine*    mod_engine{nullptr};    // null = modulator msgs silently dropped
        modulator_registry*  ext_registry{nullptr};  // user-defined modulator types; null = none
        midi_io*             midi{nullptr};          // null = SysEx/MTS/CC frames silently dropped
        osc_server*          osc{nullptr};           // null = msg_osc frames silently dropped
        luajit_participant*  lua{nullptr};           // null = :fennel eval returns error

        // Called when :vcvrack-tty dest receives a result.  Argument is a fully-formed
        // msg_repl_eval_response (0x56) EDN payload: {:result <val> :error nil|"..."}.
        // The VCVBridgeModule (Phase 2) sets this to write back into the shm frame.
        // Null = discard the result silently.
        std::function<void(std::string_view)> tty_sink;

        // Called when msg_wasm_hot_swap (0x44) arrives from a nous client.
        // Arguments: (node_id, wasm_path) — node_id matches the :node-id keyword
        // in the IPC map (e.g. "vcv-slot-0"); wasm_path is an absolute fs path.
        // Returns true if the swap was accepted; false = error (logged, nak sent).
        // Null = log and silently discard.
        std::function<bool(std::string_view node_id, std::string_view wasm_path)> wasm_swap_fn;
    };

    explicit rt_control_thread(config cfg, param_queue& queue, input_event_queue& in_queue);
    virtual ~rt_control_thread();

    rt_control_thread(const rt_control_thread&)            = delete;
    rt_control_thread& operator=(const rt_control_thread&) = delete;

    void start();
    void stop();
    bool running() const noexcept;

    // Push an outbound frame to the currently-connected client (if any).
    // Safe to call from any thread — serialised by write_mutex_.
    void push_frame(uint8_t type, std::string_view payload);

    // Dispatch a ctrl frame received out-of-band (e.g. from VCVBridgeModule via shm).
    // header_and_payload must be at least 8 bytes (IPC header) followed by the payload.
    // Responses go to cfg_.tty_sink rather than the socket (conn_fd = -1 path).
    // Thread-safe; may be called from any thread.
    void dispatch_ctrl_frame(const uint8_t* header_and_payload, uint32_t total_len);

    // Update the tty_sink callback.
    // Not thread-safe against dispatch_message — call only from the control thread
    // (e.g. from dispatch_extension after msg_graph_load) or before start().
    void set_tty_sink(std::function<void(std::string_view)> fn);

    // Set (or replace) the WASM hot-swap callback before start().
    // Called from derived classes or main() to wire vcv.hot-swap! in Fennel.
    void set_wasm_swap_fn(std::function<bool(std::string_view, std::string_view)> fn);

  protected:
    // Override to handle message types not known to the base.
    // Default implementation is a no-op.
    virtual void dispatch_extension(int conn_fd, const ipc::message& msg,
                                    std::optional<session>& sess);

  private:
    void run();
    void handle_connection(int conn_fd);
    void dispatch_message(int conn_fd, const ipc::message& msg, std::optional<session>& sess);

    config             cfg_;
    param_queue&       queue_;
    input_event_queue& in_queue_;

    // Owned LuaJIT participant — created lazily in start() when cfg_.lua is null
    // and LuaJIT is available (NOMOS_RT_HAS_LUAJIT).  Outlives the run thread
    // because stop() joins before this member is destroyed.
    std::unique_ptr<luajit_participant> lua_owned_;

    int               listen_fd_{-1};
    std::atomic<bool> running_{false};
    std::thread       thread_;

    std::mutex write_mutex_;
    int        conn_fd_write_{-1};
};

} // namespace nomos::rt
