// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <nomos/rt/osc_encode.hpp>
#include <nomos/rt/small_vector.hpp>
#include <nomos/rt/spsc_queue.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace nomos::rt {

// Maximum encoded OSC message size carried inline through the RT path. A
// control-rate OSC message (address + a few args) sits well under this; one
// that would exceed it is rejected at encode time rather than allocating on
// the RT path.
constexpr std::size_t osc_event_max_bytes = 256;

// A fully-resolved, pre-encoded outbound OSC datagram: destination endpoint
// plus the OSC wire bytes. Trivially copyable (sockaddr_in is a C struct;
// small_vector<uint8_t, N> is trivially copyable), so it rides an spsc_queue —
// and the scheduler's staging queue — by value with no heap.
struct osc_event {
    sockaddr_in                                     dest{};
    small_vector<std::uint8_t, osc_event_max_bytes> bytes{};
};

// Queue of ready-to-send OSC datagrams: pushed by the scheduler (audio/event
// thread) or the control thread, drained by the IO/sender thread → send_udp.
constexpr std::size_t osc_out_queue_capacity = 256;
using osc_out_queue                          = spsc_queue<osc_event, osc_out_queue_capacity>;

// Resolve host:port into `out`. Accepts dotted-quad and "localhost" (hostname
// lookup is out of scope — nous resolves to an IP). Returns false on failure.
inline bool resolve_osc_endpoint(std::string_view host, std::uint16_t port,
                                 sockaddr_in& out) noexcept {
    out            = sockaddr_in{};
    out.sin_family = AF_INET;
    out.sin_port   = htons(port);
    char host_z[64];
    if (host.size() >= sizeof(host_z))
        return false;
    std::memcpy(host_z, host.data(), host.size());
    host_z[host.size()] = '\0';
    const char* host_c  = (host == "localhost") ? "127.0.0.1" : host_z;
    return ::inet_pton(AF_INET, host_c, &out.sin_addr) == 1;
}

// Resolve the endpoint and encode the message into `out` in one step. Returns
// false (leaving `out` unusable) if the host is unresolvable or the message
// overflows osc_event_max_bytes. Runs on the control thread — never the audio
// thread — so this synchronous encode is off the RT path.
inline bool build_osc_event(std::string_view host, std::uint16_t port, std::string_view address,
                            const osc::arg* args, std::size_t n, osc_event& out) noexcept {
    if (!resolve_osc_endpoint(host, port, out.dest))
        return false;
    return osc::encode(out.bytes, address, args, n);
}

} // namespace nomos::rt
