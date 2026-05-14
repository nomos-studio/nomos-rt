// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <nomos/rt/ipc.hpp>
#include <nomos/rt/result.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace nomos::rt::ipc {

// ---------------------------------------------------------------------------
// Errors
// ---------------------------------------------------------------------------

enum class channel_error {
    eof,             // peer closed the connection cleanly
    io_error,        // read/write syscall failed (check errno)
    frame_too_large, // payload_len exceeds max_payload_bytes
};

// Hard cap on payload size — prevents runaway allocation on malformed frames.
constexpr std::uint32_t max_payload_bytes = 1u << 20; // 1 MiB

// ---------------------------------------------------------------------------
// message — a fully-read frame
// ---------------------------------------------------------------------------

struct message {
    header                 hdr;
    std::vector<std::byte> payload;

    std::uint8_t type() const noexcept { return hdr.type; }
};

// ---------------------------------------------------------------------------
// read_message / write_message
//
// Both functions perform blocking I/O on `fd` (a connected stream socket or
// pipe).  Partial reads/writes are retried internally until the full frame is
// transferred or an unrecoverable error occurs.
// ---------------------------------------------------------------------------

result<message, channel_error> read_message(int fd);

result<std::monostate, channel_error> write_message(int fd, std::uint8_t type,
                                                    std::span<const std::byte> payload = {});

// Convenience overload for string payloads (e.g. EDN text).
result<std::monostate, channel_error> write_message(int fd, std::uint8_t type,
                                                    std::string_view payload);

} // namespace nomos::rt::ipc
