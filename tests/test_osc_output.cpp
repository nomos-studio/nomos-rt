// SPDX-License-Identifier: LGPL-2.1-or-later
#include <catch2/catch_test_macros.hpp>

#include "osc_server.hpp"

#include <nomos/rt/input_event.hpp>
#include <nomos/rt/osc_encode.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>

using nomos::rt::input_event_queue;
using nomos::rt::osc_server;
using nomos::rt::small_vector;
namespace osc = nomos::rt::osc;

TEST_CASE("osc_server::send_osc delivers an encoded datagram to the endpoint", "[osc_output]") {
    // Receiver socket on an OS-assigned loopback port.
    const int rx = ::socket(AF_INET, SOCK_DGRAM, 0);
    REQUIRE(rx >= 0);
    sockaddr_in raddr{};
    raddr.sin_family = AF_INET;
    raddr.sin_port   = htons(0);
    ::inet_pton(AF_INET, "127.0.0.1", &raddr.sin_addr);
    REQUIRE(::bind(rx, reinterpret_cast<sockaddr*>(&raddr), sizeof(raddr)) == 0);
    socklen_t rlen = sizeof(raddr);
    REQUIRE(::getsockname(rx, reinterpret_cast<sockaddr*>(&raddr), &rlen) == 0);
    const uint16_t rx_port = ntohs(raddr.sin_port);

    // 500ms receive timeout so a lost datagram can't hang the test.
    struct timeval tv {
        0, 500'000
    };
    ::setsockopt(rx, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // Server on an OS-assigned port — we only use its socket to send.
    input_event_queue q;
    osc_server        server{0, q};
    server.start();

    const osc::arg args[] = {osc::arg::make_i(7), osc::arg::make_f(1.0f), osc::arg::make_s("hi")};
    REQUIRE(server.send_osc("127.0.0.1", rx_port, "/x", args, 3));

    uint8_t       buf[256];
    const ssize_t n = ::recvfrom(rx, buf, sizeof(buf), 0, nullptr, nullptr);
    REQUIRE(n > 0);

    // The datagram must be exactly the canonical OSC encoding.
    small_vector<uint8_t, 256> want;
    REQUIRE(osc::encode(want, "/x", args, 3));
    REQUIRE(static_cast<std::size_t>(n) == want.size());
    REQUIRE(std::memcmp(buf, want.data(), want.size()) == 0);

    server.stop();
    ::close(rx);
}

TEST_CASE("osc_server::send_osc resolves \"localhost\"", "[osc_output]") {
    const int rx = ::socket(AF_INET, SOCK_DGRAM, 0);
    REQUIRE(rx >= 0);
    sockaddr_in raddr{};
    raddr.sin_family = AF_INET;
    raddr.sin_port   = htons(0);
    ::inet_pton(AF_INET, "127.0.0.1", &raddr.sin_addr);
    REQUIRE(::bind(rx, reinterpret_cast<sockaddr*>(&raddr), sizeof(raddr)) == 0);
    socklen_t rlen = sizeof(raddr);
    REQUIRE(::getsockname(rx, reinterpret_cast<sockaddr*>(&raddr), &rlen) == 0);
    const uint16_t rx_port = ntohs(raddr.sin_port);
    struct timeval tv {
        0, 500'000
    };
    ::setsockopt(rx, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    input_event_queue q;
    osc_server        server{0, q};
    server.start();

    REQUIRE(server.send_osc("localhost", rx_port, "/ping", nullptr, 0));
    uint8_t       buf[64];
    const ssize_t n = ::recvfrom(rx, buf, sizeof(buf), 0, nullptr, nullptr);
    REQUIRE(n > 0);

    server.stop();
    ::close(rx);
}

TEST_CASE("osc_server::send_osc rejects an unresolvable host", "[osc_output]") {
    input_event_queue q;
    osc_server        server{0, q};
    server.start();
    const osc::arg args[] = {osc::arg::make_i(1)};
    REQUIRE(!server.send_osc("not-an-ip", 9000, "/x", args, 1));
    server.stop();
}
