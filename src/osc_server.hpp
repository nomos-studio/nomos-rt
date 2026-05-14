// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <nomos/rt/input_event.hpp>

#include <atomic>
#include <cstdint>
#include <thread>

namespace nomos::rt {

// Minimal UDP OSC server.  Listens on `port` and converts a small set of
// /cljseq messages into clap_event_union values pushed to `queue`.
//
// Supported addresses (all events land at sample offset 0 in the next block):
//   /cljseq/note/on   ,iiif   port(i), channel(i), key(i), velocity(f, 0..1)
//   /cljseq/note/off  ,iii    port(i), channel(i), key(i)
//   /cljseq/midi      ,iiii   port(i), b0(i), b1(i), b2(i)
class osc_server {
  public:
    osc_server(uint16_t port, input_event_queue& queue);
    ~osc_server();

    osc_server(const osc_server&)            = delete;
    osc_server& operator=(const osc_server&) = delete;

    void start();
    void stop();
    bool running() const noexcept;

  private:
    void run();
    void handle_packet(const uint8_t* buf, std::size_t len) noexcept;

    uint16_t           port_;
    input_event_queue& queue_;
    int                sock_{-1};
    std::atomic<bool>  running_{false};
    std::thread        thread_;
};

} // namespace nomos::rt
