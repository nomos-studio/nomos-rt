// SPDX-License-Identifier: LGPL-2.1-or-later
#include <nomos/rt/ipc_channel.hpp>

#include <cerrno>
#include <cstring>
#include <variant>

#include <sys/socket.h>
#include <unistd.h>

namespace nomos::rt::ipc {

namespace {

    // Read exactly `n` bytes into `buf`.  Returns false on EOF or error.
    bool read_exact(int fd, void* buf, std::size_t n) {
        auto*       p   = static_cast<std::byte*>(buf);
        std::size_t rem = n;
        while (rem > 0) {
            const ssize_t r = ::read(fd, p, rem);
            if (r <= 0)
                return false;
            p += r;
            rem -= static_cast<std::size_t>(r);
        }
        return true;
    }

    // Write exactly `n` bytes from `buf`.  Returns false on error.
    bool write_exact(int fd, const void* buf, std::size_t n) {
        const auto* p   = static_cast<const std::byte*>(buf);
        std::size_t rem = n;
        while (rem > 0) {
            const ssize_t w = ::write(fd, p, rem);
            if (w <= 0)
                return false;
            p += w;
            rem -= static_cast<std::size_t>(w);
        }
        return true;
    }

} // namespace

result<message, channel_error> read_message(int fd) {
    header hdr{};
    if (!read_exact(fd, &hdr, sizeof(hdr))) {
        if (errno == 0 || errno == ECONNRESET)
            return unexpected<channel_error>{channel_error::eof};
        return unexpected<channel_error>{channel_error::io_error};
    }

    // payload_len is big-endian on the wire; swap on little-endian hosts.
    const std::uint32_t len = __builtin_bswap32(hdr.payload_len);

    if (len > max_payload_bytes)
        return unexpected<channel_error>{channel_error::frame_too_large};

    // Fix up the header's payload_len to host byte order before returning.
    hdr.payload_len = len;

    std::vector<std::byte> payload(len);
    if (len > 0 && !read_exact(fd, payload.data(), len)) {
        if (errno == 0 || errno == ECONNRESET)
            return unexpected<channel_error>{channel_error::eof};
        return unexpected<channel_error>{channel_error::io_error};
    }

    return message{hdr, std::move(payload)};
}

result<std::monostate, channel_error> write_message(int fd, std::uint8_t type,
                                                    std::span<const std::byte> payload) {
    const auto len = static_cast<std::uint32_t>(payload.size());

    header hdr{};
    hdr.payload_len = __builtin_bswap32(len); // big-endian on the wire
    hdr.type        = type;

    if (!write_exact(fd, &hdr, sizeof(hdr)))
        return unexpected<channel_error>{channel_error::io_error};

    if (len > 0 && !write_exact(fd, payload.data(), len))
        return unexpected<channel_error>{channel_error::io_error};

    return std::monostate{};
}

result<std::monostate, channel_error> write_message(int fd, std::uint8_t type,
                                                    std::string_view payload) {
    return write_message(fd, type,
                         std::span<const std::byte>{
                             reinterpret_cast<const std::byte*>(payload.data()), payload.size()});
}

} // namespace nomos::rt::ipc
