// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <nomos/rt/small_vector.hpp>

#include <cstdint>
#include <cstring>
#include <string_view>

namespace nomos::rt::osc {

// One OSC argument, tagged by its OSC type char. Non-owning for strings — the
// referenced data must outlive the encode() call (which is synchronous).
struct arg {
    char             tag; // 'i' int32, 'f' float32, 's' string
    std::int32_t     i = 0;
    float            f = 0.0f;
    std::string_view s = {};

    static arg make_i(std::int32_t v) noexcept { return {.tag = 'i', .i = v}; }
    static arg make_f(float v) noexcept { return {.tag = 'f', .f = v}; }
    static arg make_s(std::string_view v) noexcept { return {.tag = 's', .s = v}; }
};

namespace detail {

    // OSC string: content bytes + at least one null terminator, zero-padded to a
    // 4-byte boundary. Returns false (no effect) if it would not fit.
    template <std::size_t N>
    inline bool append_osc_string(small_vector<std::uint8_t, N>& out, std::string_view s) noexcept {
        const std::size_t padded = ((s.size() / 4) + 1) * 4; // > len, 4-aligned
        if (padded > out.capacity() - out.size())
            return false;
        out.append(reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
        for (std::size_t k = s.size(); k < padded; ++k)
            out.push_back(0);
        return true;
    }

    // 32-bit big-endian (OSC network byte order).
    template <std::size_t N>
    inline bool append_u32_be(small_vector<std::uint8_t, N>& out, std::uint32_t v) noexcept {
        const std::uint8_t b[4] = {static_cast<std::uint8_t>((v >> 24) & 0xFF),
                                   static_cast<std::uint8_t>((v >> 16) & 0xFF),
                                   static_cast<std::uint8_t>((v >> 8) & 0xFF),
                                   static_cast<std::uint8_t>(v & 0xFF)};
        return out.append(b, 4);
    }

} // namespace detail

// Encode an OSC message (address + args) into `out`, replacing its contents.
// Wire layout: <address,padded> <",tags",padded> <arg0><arg1>…
// Returns false (and leaves `out` cleared/partial) if the message does not fit
// the buffer's fixed capacity — the caller decides how to handle over-long
// payloads. All numeric args are big-endian.
template <std::size_t N>
inline bool encode(small_vector<std::uint8_t, N>& out, std::string_view address, const arg* args,
                   std::size_t n) noexcept {
    out.clear();

    if (!detail::append_osc_string(out, address))
        return false;

    // Type-tag string: ',' + one char per arg, then null-pad.
    if (!out.push_back(static_cast<std::uint8_t>(',')))
        return false;
    for (std::size_t k = 0; k < n; ++k)
        if (!out.push_back(static_cast<std::uint8_t>(args[k].tag)))
            return false;
    // Pad the ",tags" run (length n+1) up to a 4-boundary with ≥1 null.
    const std::size_t tags_len    = n + 1;
    const std::size_t tags_padded = ((tags_len / 4) + 1) * 4;
    for (std::size_t k = tags_len; k < tags_padded; ++k)
        if (!out.push_back(0))
            return false;

    for (std::size_t k = 0; k < n; ++k) {
        const arg& a = args[k];
        switch (a.tag) {
        case 'i':
            if (!detail::append_u32_be(out, static_cast<std::uint32_t>(a.i)))
                return false;
            break;
        case 'f': {
            std::uint32_t bits;
            std::memcpy(&bits, &a.f, 4);
            if (!detail::append_u32_be(out, bits))
                return false;
            break;
        }
        case 's':
            if (!detail::append_osc_string(out, a.s))
                return false;
            break;
        default:
            return false; // unknown tag
        }
    }
    return true;
}

} // namespace nomos::rt::osc
