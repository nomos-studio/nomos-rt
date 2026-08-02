// SPDX-License-Identifier: LGPL-2.1-or-later
#include <catch2/catch_test_macros.hpp>

#include <nomos/rt/event_scheduler.hpp>
#include <nomos/rt/osc_event.hpp>

#include <clap/events.h>

#include <cstdint>
#include <cstring>
#include <type_traits>

using nomos::rt::build_osc_event;
using nomos::rt::clap_event_union;
using nomos::rt::event_scheduler;
using nomos::rt::osc_event;
using nomos::rt::osc_out_queue;
using nomos::rt::sched_kind;
using nomos::rt::scheduled_event;
using nomos::rt::small_vector;
namespace osc = nomos::rt::osc;

TEST_CASE("scheduler routes osc-kind events to osc_out_queue, not the callback",
          "[scheduled_osc]") {
    event_scheduler sched;
    osc_out_queue   q;
    sched.set_osc_out(&q);

    // Schedule an OSC event at beat 1.0.
    osc_event      oe;
    const osc::arg args[] = {osc::arg::make_i(7), osc::arg::make_s("hi")};
    REQUIRE(build_osc_event("127.0.0.1", 9000, "/x", args, 2, oe));
    sched.staging().push(scheduled_event{.beat = 1.0, .kind = sched_kind::osc, .osc = oe});

    // Schedule a CLAP note at the same beat to prove the callback still fires.
    clap_event_union note{};
    note.note.header.type = CLAP_EVENT_NOTE_ON;
    sched.staging().push(scheduled_event{.beat = 1.0, .event = note});

    int clap_fired = 0;
    sched.tick(2.0, [&](const clap_event_union&) { ++clap_fired; });

    REQUIRE(clap_fired == 1); // CLAP event delivered via the callback

    auto out = q.pop(); // OSC event routed to the queue instead
    REQUIRE(out.has_value());

    // The queued datagram matches the canonical encoding.
    small_vector<std::uint8_t, 256> want;
    REQUIRE(osc::encode(want, "/x", args, 2));
    REQUIRE(out->bytes.size() == want.size());
    REQUIRE(std::memcmp(out->bytes.data(), want.data(), want.size()) == 0);
}

TEST_CASE("scheduler fires osc and clap events in beat order", "[scheduled_osc]") {
    event_scheduler sched;
    osc_out_queue   q;
    sched.set_osc_out(&q);

    osc_event      oe;
    const osc::arg a[] = {osc::arg::make_i(1)};
    REQUIRE(build_osc_event("127.0.0.1", 9000, "/late", a, 1, oe));

    // OSC at beat 3.0 (future), note at beat 1.0.
    sched.staging().push(scheduled_event{.beat = 3.0, .kind = sched_kind::osc, .osc = oe});
    clap_event_union note{};
    sched.staging().push(scheduled_event{.beat = 1.0, .event = note});

    // Tick to 2.0 — only the note is due.
    int fired = 0;
    sched.tick(2.0, [&](const clap_event_union&) { ++fired; });
    REQUIRE(fired == 1);
    REQUIRE(!q.pop().has_value()); // OSC not yet due

    // Tick to 3.0 — OSC now due.
    sched.tick(3.0, [&](const clap_event_union&) { ++fired; });
    REQUIRE(fired == 1);          // no more notes
    REQUIRE(q.pop().has_value()); // OSC fired
}

TEST_CASE("scheduler drops osc events when no osc_out_queue is wired", "[scheduled_osc]") {
    event_scheduler sched; // set_osc_out never called
    osc_event       oe;
    const osc::arg  a[] = {osc::arg::make_i(1)};
    REQUIRE(build_osc_event("127.0.0.1", 9000, "/y", a, 1, oe));
    sched.staging().push(scheduled_event{.beat = 1.0, .kind = sched_kind::osc, .osc = oe});

    int fired = 0;
    sched.tick(2.0, [&](const clap_event_union&) { ++fired; }); // must not crash
    REQUIRE(fired == 0);
}

TEST_CASE("osc_event and scheduled_event are trivially copyable", "[scheduled_osc]") {
    static_assert(std::is_trivially_copyable_v<osc_event>,
                  "osc_event must ride an spsc_queue by value");
    static_assert(std::is_trivially_copyable_v<scheduled_event>,
                  "scheduled_event must ride the staging queue by value");
    SUCCEED();
}
