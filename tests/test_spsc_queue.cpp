// SPDX-License-Identifier: LGPL-2.1-or-later
#include <catch2/catch_test_macros.hpp>

#include <nomos/rt/spsc_queue.hpp>

#include <thread>
#include <vector>

TEST_CASE("spsc_queue: empty on construction", "[spsc_queue]") {
    nomos::rt::spsc_queue<int, 4> q;
    REQUIRE(q.empty());
    REQUIRE(q.size() == 0);
    REQUIRE(!q.pop().has_value());
}

TEST_CASE("spsc_queue: push and pop round-trip", "[spsc_queue]") {
    nomos::rt::spsc_queue<int, 4> q;
    REQUIRE(q.push(1));
    REQUIRE(q.push(2));
    REQUIRE(q.push(3));
    REQUIRE(!q.push(4)); // capacity - 1 = 3 slots usable

    auto v1 = q.pop();
    REQUIRE(v1.has_value());
    REQUIRE(*v1 == 1);

    auto v2 = q.pop();
    REQUIRE(v2.has_value());
    REQUIRE(*v2 == 2);
}

TEST_CASE("spsc_queue: producer/consumer threads", "[spsc_queue]") {
    nomos::rt::spsc_queue<int, 64> q;
    constexpr int                  n = 1000;

    std::thread producer([&] {
        for (int i = 0; i < n; ++i) {
            while (!q.push(i))
                std::this_thread::yield();
        }
    });

    std::vector<int> received;
    received.reserve(n);
    while (static_cast<int>(received.size()) < n) {
        if (auto v = q.pop())
            received.push_back(*v);
        else
            std::this_thread::yield();
    }

    producer.join();

    REQUIRE(static_cast<int>(received.size()) == n);
    for (int i = 0; i < n; ++i)
        REQUIRE(received[i] == i);
}
