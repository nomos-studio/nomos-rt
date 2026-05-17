// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <nomos/rt/input_event.hpp>
#include <nomos/rt/ipc_channel.hpp>
#include <nomos/rt/param_event.hpp>
#include <nomos/rt/scheduled_event.hpp>
#include <nomos/rt/session.hpp>
#include <nomos/rt/spsc_queue.hpp>

// Forward declarations — avoid pulling rcu/urcu into consumers that only need the config.
namespace nomos::rt {
class modulator_engine;
class modulator_registry;
}

#include <atomic>
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
// dispatches on the common cljseq-rt message types:
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
        std::string          socket_path;             // Unix domain socket path
        std::string          db_path;                 // txlog database path
        sched_staging_queue* sched_staging{nullptr};  // null = immediate dispatch
        modulator_engine*    mod_engine{nullptr};     // null = modulator msgs silently dropped
        modulator_registry*  ext_registry{nullptr};   // user-defined modulator types; null = none
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

    int               listen_fd_{-1};
    std::atomic<bool> running_{false};
    std::thread       thread_;

    std::mutex write_mutex_;
    int        conn_fd_write_{-1};
};

} // namespace nomos::rt
