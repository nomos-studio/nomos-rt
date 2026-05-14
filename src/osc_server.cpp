// SPDX-License-Identifier: GPL-2.0-or-later
#include "osc_server.hpp"

#include <clap/events.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <string_view>

namespace nomos::rt {

namespace {

    inline int32_t osc_int32(const uint8_t* p) noexcept {
        uint32_t v;
        std::memcpy(&v, p, 4);
        return static_cast<int32_t>(ntohl(v));
    }

    inline float osc_float(const uint8_t* p) noexcept {
        uint32_t v;
        std::memcpy(&v, p, 4);
        v = ntohl(v);
        float f;
        std::memcpy(&f, &v, 4);
        return f;
    }

    inline std::size_t pad4(std::size_t n) noexcept {
        return (n + 3) & ~std::size_t{3};
    }

} // namespace

osc_server::osc_server(uint16_t port, input_event_queue& queue) : port_(port), queue_(queue) {
}

osc_server::~osc_server() {
    stop();
}

void osc_server::start() {
    sock_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_ < 0)
        return;

    // 100ms receive timeout so the thread wakes up to check the stop flag.
    struct timeval tv{0, 100'000};
    ::setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port_);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (::bind(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(sock_);
        sock_ = -1;
        return;
    }

    running_.store(true, std::memory_order_release);
    thread_ = std::thread(&osc_server::run, this);
}

void osc_server::stop() {
    if (!running_.exchange(false, std::memory_order_acq_rel))
        return;
    if (sock_ >= 0) {
        ::close(sock_);
        sock_ = -1;
    }
    if (thread_.joinable())
        thread_.join();
}

bool osc_server::running() const noexcept {
    return running_.load(std::memory_order_acquire);
}

void osc_server::run() {
    constexpr std::size_t k_buf = 1500;
    uint8_t               buf[k_buf];

    while (running_.load(std::memory_order_acquire)) {
        const ssize_t n = ::recv(sock_, buf, sizeof(buf), 0);
        if (n <= 0)
            continue; // timeout or error; re-check running flag
        handle_packet(buf, static_cast<std::size_t>(n));
    }
}

void osc_server::handle_packet(const uint8_t* buf, std::size_t len) noexcept {
    if (len < 8)
        return;

    // Address string: null-terminated, padded to 4 bytes.
    const char*       addr_cstr = reinterpret_cast<const char*>(buf);
    const std::size_t addr_len  = ::strnlen(addr_cstr, len);
    if (addr_len >= len)
        return;
    const std::size_t tags_off = pad4(addr_len + 1);
    if (tags_off >= len)
        return;

    // Type tag string: starts with ',', padded to 4 bytes.
    const char* tags_cstr = reinterpret_cast<const char*>(buf + tags_off);
    if (tags_cstr[0] != ',')
        return;
    const std::size_t tags_len = ::strnlen(tags_cstr, len - tags_off);
    const std::size_t args_off = tags_off + pad4(tags_len + 1);

    const std::string_view address{addr_cstr, addr_len};
    const std::string_view types{tags_cstr + 1, tags_len - 1}; // skip ','
    const uint8_t*         args  = buf + args_off;
    const std::size_t      avail = (args_off < len) ? len - args_off : 0;

    if (address == "/cljseq/note/on" && types == "iiif" && avail >= 16) {
        clap_event_union ev{};
        ev.note.header.size     = sizeof(clap_event_note_t);
        ev.note.header.time     = 0;
        ev.note.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        ev.note.header.type     = CLAP_EVENT_NOTE_ON;
        ev.note.header.flags    = 0;
        ev.note.note_id         = -1;
        ev.note.port_index      = static_cast<int16_t>(osc_int32(args));
        ev.note.channel         = static_cast<int16_t>(osc_int32(args + 4));
        ev.note.key             = static_cast<int16_t>(osc_int32(args + 8));
        ev.note.velocity        = static_cast<double>(osc_float(args + 12));
        queue_.push(ev);

    } else if (address == "/cljseq/note/off" && types == "iii" && avail >= 12) {
        clap_event_union ev{};
        ev.note.header.size     = sizeof(clap_event_note_t);
        ev.note.header.time     = 0;
        ev.note.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        ev.note.header.type     = CLAP_EVENT_NOTE_OFF;
        ev.note.header.flags    = 0;
        ev.note.note_id         = -1;
        ev.note.port_index      = static_cast<int16_t>(osc_int32(args));
        ev.note.channel         = static_cast<int16_t>(osc_int32(args + 4));
        ev.note.key             = static_cast<int16_t>(osc_int32(args + 8));
        ev.note.velocity        = 0.0;
        queue_.push(ev);

    } else if (address == "/cljseq/midi" && types == "iiii" && avail >= 16) {
        clap_event_union ev{};
        ev.midi.header.size     = sizeof(clap_event_midi_t);
        ev.midi.header.time     = 0;
        ev.midi.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        ev.midi.header.type     = CLAP_EVENT_MIDI;
        ev.midi.header.flags    = 0;
        ev.midi.port_index      = static_cast<uint16_t>(osc_int32(args));
        ev.midi.data[0]         = static_cast<uint8_t>(osc_int32(args + 4));
        ev.midi.data[1]         = static_cast<uint8_t>(osc_int32(args + 8));
        ev.midi.data[2]         = static_cast<uint8_t>(osc_int32(args + 12));
        queue_.push(ev);
    }
}

} // namespace nomos::rt
