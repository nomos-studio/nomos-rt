// SPDX-License-Identifier: LGPL-2.1-or-later
#include <catch2/catch_test_macros.hpp>

#include <nomos/rt/osc_encode.hpp>
#include <nomos/rt/small_vector.hpp>

#include <cstdint>
#include <vector>

using nomos::rt::small_vector;
namespace osc = nomos::rt::osc;

namespace {
std::vector<std::uint8_t> bytes(const small_vector<std::uint8_t, 256>& v) {
    return {v.begin(), v.end()};
}
} // namespace

TEST_CASE("osc_encode: address only, no args", "[osc_encode]") {
    small_vector<std::uint8_t, 256> out;
    REQUIRE(osc::encode(out, "/foo", nullptr, 0));
    // "/foo" (4) → padded to 8 (needs ≥1 null); ",\0\0\0" tags (len 1 → 4).
    const std::vector<std::uint8_t> want = {'/', 'f', 'o', 'o', 0, 0, 0, 0, ',', 0, 0, 0};
    REQUIRE(bytes(out) == want);
    REQUIRE(out.size() % 4 == 0);
}

TEST_CASE("osc_encode: int32 arg is big-endian", "[osc_encode]") {
    small_vector<std::uint8_t, 256> out;
    const osc::arg                  args[] = {osc::arg::make_i(5)};
    REQUIRE(osc::encode(out, "/x", args, 1));
    // "/x\0\0" (2→4) + ",i\0\0" (tags len 2 → 4) + 00 00 00 05
    const std::vector<std::uint8_t> want = {'/', 'x', 0, 0, ',', 'i', 0, 0, 0, 0, 0, 5};
    REQUIRE(bytes(out) == want);
}

TEST_CASE("osc_encode: float32 arg is IEEE-754 big-endian", "[osc_encode]") {
    small_vector<std::uint8_t, 256> out;
    const osc::arg                  args[] = {osc::arg::make_f(1.0f)}; // 0x3F800000
    REQUIRE(osc::encode(out, "/y", args, 1));
    const std::vector<std::uint8_t> want = {'/', 'y', 0, 0, ',', 'f', 0, 0, 0x3F, 0x80, 0x00, 0x00};
    REQUIRE(bytes(out) == want);
}

TEST_CASE("osc_encode: string arg is null-padded to 4", "[osc_encode]") {
    small_vector<std::uint8_t, 256> out;
    const osc::arg                  args[] = {osc::arg::make_s("hi")}; // 2 chars → pad to 4
    REQUIRE(osc::encode(out, "/s", args, 1));
    // "/s\0\0" + ",s\0\0" + "hi\0\0"
    const std::vector<std::uint8_t> want = {'/', 's', 0, 0, ',', 's', 0, 0, 'h', 'i', 0, 0};
    REQUIRE(bytes(out) == want);
}

TEST_CASE("osc_encode: mixed args in order, tags match", "[osc_encode]") {
    small_vector<std::uint8_t, 256> out;
    const osc::arg args[] = {osc::arg::make_i(7), osc::arg::make_f(0.0f), osc::arg::make_s("ok")};
    REQUIRE(osc::encode(out, "/mix", args, 3));
    // tags ",ifs" (len 4 → padded to 8: ',','i','f','s',0,0,0,0)
    const std::vector<std::uint8_t> want = {
        '/', 'm', 'i', 'x', 0, 0, 0, 0, // address
        ',', 'i', 'f', 's', 0, 0, 0, 0, // type tags (len 4 → 8)
        0,   0,   0,   7,               // i 7
        0,   0,   0,   0,               // f 0.0
        'o', 'k', 0,   0,               // s "ok"
    };
    REQUIRE(bytes(out) == want);
}

TEST_CASE("osc_encode: over-capacity returns false", "[osc_encode]") {
    small_vector<std::uint8_t, 8> tiny; // address alone needs 8+
    REQUIRE(!osc::encode(tiny, "/toolong", nullptr, 0));
}
