// SPDX-License-Identifier: LGPL-2.1-or-later
#include <catch2/catch_test_macros.hpp>

#include <nomos/rt/small_vector.hpp>
#include <nomos/rt/spsc_queue.hpp>

#include <cstdint>
#include <type_traits>

using nomos::rt::small_vector;

TEST_CASE("small_vector: empty on construction", "[small_vector]") {
    small_vector<std::uint8_t, 8> v;
    REQUIRE(v.empty());
    REQUIRE(v.size() == 0);
    REQUIRE(!v.full());
    REQUIRE(v.capacity() == 8);
}

TEST_CASE("small_vector: push_back fills to capacity then refuses", "[small_vector]") {
    small_vector<int, 3> v;
    REQUIRE(v.push_back(10));
    REQUIRE(v.push_back(20));
    REQUIRE(v.push_back(30));
    REQUIRE(v.full());
    REQUIRE(!v.push_back(40)); // full: no effect
    REQUIRE(v.size() == 3);
    REQUIRE(v[0] == 10);
    REQUIRE(v[1] == 20);
    REQUIRE(v[2] == 30);
}

TEST_CASE("small_vector: append is all-or-nothing", "[small_vector]") {
    small_vector<std::uint8_t, 4> v;
    const std::uint8_t            two[]   = {0xAA, 0xBB};
    const std::uint8_t            three[] = {1, 2, 3};

    REQUIRE(v.append(two, 2));
    REQUIRE(v.size() == 2);

    // 3 more would exceed capacity 4 → rejected, nothing appended
    REQUIRE(!v.append(three, 3));
    REQUIRE(v.size() == 2);

    // exactly fills the remaining 2
    REQUIRE(v.append(three, 2));
    REQUIRE(v.size() == 4);
    REQUIRE(v.full());
    REQUIRE(v[2] == 1);
    REQUIRE(v[3] == 2);
}

TEST_CASE("small_vector: clear resets size, keeps capacity", "[small_vector]") {
    small_vector<int, 4> v{1, 2, 3};
    REQUIRE(v.size() == 3);
    v.clear();
    REQUIRE(v.empty());
    REQUIRE(v.capacity() == 4);
    REQUIRE(v.push_back(9));
    REQUIRE(v[0] == 9);
}

TEST_CASE("small_vector: initializer_list truncates at capacity", "[small_vector]") {
    small_vector<int, 2> v{1, 2, 3, 4};
    REQUIRE(v.size() == 2);
    REQUIRE(v[0] == 1);
    REQUIRE(v[1] == 2);
}

TEST_CASE("small_vector: iteration and data() over the live range", "[small_vector]") {
    small_vector<std::uint8_t, 8> v;
    const std::uint8_t            bytes[] = {5, 6, 7};
    REQUIRE(v.append(bytes, 3));

    int sum = 0;
    for (auto b : v)
        sum += b;
    REQUIRE(sum == 18);
    REQUIRE(v.data()[0] == 5);
    REQUIRE(static_cast<std::size_t>(v.end() - v.begin()) == 3);
}

TEST_CASE("small_vector: trivially copyable for trivial T (queue-carriable)", "[small_vector]") {
    // The immediate OSC path carries the payload inline through an spsc_queue,
    // which requires the element type be trivially copyable.
    static_assert(std::is_trivially_copyable_v<small_vector<std::uint8_t, 256>>,
                  "small_vector<byte, N> must be trivially copyable to ride the queue");

    nomos::rt::spsc_queue<small_vector<std::uint8_t, 16>, 4> q;
    small_vector<std::uint8_t, 16>                           msg;
    const std::uint8_t                                       bytes[] = {0xDE, 0xAD, 0xBE, 0xEF};
    REQUIRE(msg.append(bytes, 4));
    REQUIRE(q.push(msg));

    auto out = q.pop();
    REQUIRE(out.has_value());
    REQUIRE(out->size() == 4);
    REQUIRE((*out)[3] == 0xEF);
}
