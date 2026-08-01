// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <nomos/rt/input_event.hpp>
#include <nomos/rt/osc_encode.hpp>

#include <netinet/in.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace nomos::rt {

// Minimal UDP OSC server.  Listens on `port` and converts a small set of
// /nous messages into clap_event_union values pushed to `queue`.
//
// Static addresses (all events land at sample offset 0 in the next block):
//   /nous/note/on   ,iiif   port(i), channel(i), key(i), velocity(f, 0..1)
//   /nous/note/off  ,iii    port(i), channel(i), key(i)
//   /nous/midi      ,iiii   port(i), b0(i), b1(i), b2(i)
//
// Pull endpoint:
//   /nous/endpoints  (no args) — server responds with /nous/endpoints ,s
//                    containing the newline-delimited list of registered Fennel
//                    OSC handlers.  Wired via dispatch_fns.get_endpoints.
//
// Fallthrough:
//   Any unmatched address is forwarded to dispatch_fns.dispatch, which calls
//   the matching handler in the Fennel nomos-osc-dispatch table (Phase 10).
class osc_server {
  public:
    osc_server(uint16_t port, input_event_queue& queue);
    ~osc_server();

    osc_server(const osc_server&)            = delete;
    osc_server& operator=(const osc_server&) = delete;

    // Callbacks wired by the caller after construction and before start().
    // Not thread-safe — call once before start().
    struct dispatch_fns {
        // Called for any path not handled by the static routes.
        // type_tags does NOT include the leading ','.
        // Returns true if a Lua handler was found and called.
        std::function<bool(std::string_view path, std::string_view type_tags, const uint8_t* args,
                           std::size_t args_len)>
            dispatch;

        // Returns newline-delimited registered OSC paths.
        // Used to build the /nous/endpoints response.
        std::function<std::string()> get_endpoints;
    };

    void set_dispatch(dispatch_fns fns);

    void start();
    void stop();
    bool running() const noexcept;

    // Send the current endpoint list to the last known OSC sender.
    // Called from the Fennel nomos_osc_push_endpoints binding (via osc.register).
    // Safe to call from any thread.
    void push_endpoints();

    // Send an OSC message to an external UDP endpoint (outbound).
    // Encodes address + args to the wire, resolves host:port (dotted-quad or
    // "localhost"), and sends via the server socket. Called from the control
    // thread on msg_osc — like msg_cc's direct midi->send(), this is off the
    // audio thread, so the synchronous sendto() is safe. Returns false if the
    // socket is closed, the host is unresolvable, or the message overflows the
    // encode buffer. Safe to call from any thread once start() has run.
    bool send_osc(std::string_view host, uint16_t port, std::string_view address,
                  const osc::arg* args, std::size_t n) noexcept;

  private:
    void run();
    void handle_packet(const uint8_t* buf, std::size_t len) noexcept;
    void send_udp(const sockaddr_in& dest, const uint8_t* data, std::size_t len) noexcept;

    uint16_t           port_;
    input_event_queue& queue_;
    dispatch_fns       dispatch_fns_;
    int                sock_{-1};
    std::atomic<bool>  running_{false};
    std::thread        thread_;

    // Last known sender — updated on every received packet; read by push_endpoints().
    sockaddr_in last_sender_{};
    bool        has_sender_{false};
    std::mutex  sender_mutex_;
};

} // namespace nomos::rt
