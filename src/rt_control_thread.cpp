// SPDX-License-Identifier: GPL-2.0-or-later
#include <nomos/rt/ipc.hpp>
#include <nomos/rt/rt_control_thread.hpp>

#include "bools_ring_modulator.hpp"
#include "cipher_modulator.hpp"
#include "cv_channel_decoder.hpp"
#include "divine_cmos_modulator.hpp"
#include "fractal_modulator.hpp"
#include "genie_modulator.hpp"
#include "graph_modulator.hpp"
#include "lets_splosh_modulator.hpp"
#include "segment_modulator.hpp"
#include "shift_register_modulator.hpp"
#include "slew_modulator.hpp"
#include "slope_modulator.hpp"
#include "sloth_chaos_modulator.hpp"
#include "squid_axon_modulator.hpp"
#include "statues_modulator.hpp"
#include "stochastic_modulator.hpp"
#include <nomos/rt/modulator_engine.hpp>
#include <nomos/rt/modulator_registry.hpp>

#include <edn/builtins.hpp>
#include <edn/parser.hpp>
#include <txlog/txlog.hpp>

#include <clap/events.h>

#include "luajit_participant.hpp"
#include "midi_io.hpp"

#include <array>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>

namespace {

// Encode one MIDI key's frequency as a 3-byte MTS note entry [xx yy zz].
// hz=0 or negative produces the 12-TET identity for that key.
std::array<uint8_t, 3> mts_encode_note(int key, double hz) noexcept {
    const double tet_hz = 440.0 * std::pow(2.0, (key - 69) / 12.0);
    const double use_hz = (hz > 0.0) ? hz : tet_hz;

    const double semitones  = 12.0 * std::log2(use_hz / 440.0) + 69.0;
    int          xx         = static_cast<int>(std::round(semitones));
    double       bend_cents = (semitones - xx) * 100.0;

    if (bend_cents < 0.0) {
        xx = std::max(0, xx - 1);
        bend_cents += 100.0;
    }
    xx = std::max(0, std::min(127, xx));

    const int fine =
        std::max(0, std::min(16383, static_cast<int>(std::round(bend_cents / 100.0 * 16384.0))));
    return {static_cast<uint8_t>(xx), static_cast<uint8_t>((fine >> 7) & 0x7F),
            static_cast<uint8_t>(fine & 0x7F)};
}

// Assemble a 408-byte MTS Bulk Dump SysEx.
// tuning: MIDI key → Hz (missing keys use 12-TET identity).
// prog: MTS tuning programme 0-127.  device_id: 0x7F = all.
std::vector<uint8_t> mts_bulk_dump(const std::unordered_map<int, double>& tuning, uint8_t prog,
                                   uint8_t device_id) {
    std::vector<uint8_t> out;
    out.reserve(408);

    out.push_back(0xF0);
    out.push_back(0x7E);
    out.push_back(device_id);
    out.push_back(0x08);
    out.push_back(0x01);
    out.push_back(prog);

    // 16-byte name field (null-padded)
    for (int i = 0; i < 16; ++i)
        out.push_back(0x00);

    // 128 × 3 bytes note entries
    for (int k = 0; k < 128; ++k) {
        double hz = 0.0;
        if (auto it = tuning.find(k); it != tuning.end())
            hz = it->second;
        const auto [xx, yy, zz] = mts_encode_note(k, hz);
        out.push_back(xx);
        out.push_back(yy);
        out.push_back(zz);
    }

    // Checksum: XOR of bytes [1] through [end-1], masked to 7 bits
    uint8_t csum = 0;
    for (std::size_t i = 1; i < out.size(); ++i)
        csum ^= out[i];
    out.push_back(csum & 0x7F);
    out.push_back(0xF7);

    return out;
}

// Parse the canonical UUID string "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" into
// bytes.
edn::uuid parse_uuid(const std::string& s) noexcept {
    edn::uuid u{};
    if (s.size() != 36)
        return u;
    int  bi = 0;
    auto h  = [](char c) -> uint8_t {
        return c <= '9' ? static_cast<uint8_t>(c - '0') : static_cast<uint8_t>((c | 32) - 'a' + 10);
    };
    for (std::size_t i = 0; i < 36 && bi < 16;) {
        if (s[i] == '-') {
            ++i;
            continue;
        }
        u.bytes[bi++] = static_cast<uint8_t>((h(s[i]) << 4) | h(s[i + 1]));
        i += 2;
    }
    return u;
}

} // namespace

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace nomos::rt {

namespace {

    int make_listen_socket(const std::string& path) {
        const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0)
            return -1;

        ::unlink(path.c_str()); // remove stale socket

        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

        if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0 ||
            ::listen(fd, 8) < 0) {
            ::close(fd);
            return -1;
        }
        return fd;
    }

    // Build a CLAP note event from its component fields.
    clap_event_union make_note_event(bool is_on, int16_t key, int16_t channel, int16_t port,
                                     int16_t note_id, double velocity) noexcept {
        clap_event_union ev{};
        ev.note.header.size     = sizeof(clap_event_note_t);
        ev.note.header.time     = 0;
        ev.note.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        ev.note.header.type     = is_on ? CLAP_EVENT_NOTE_ON : CLAP_EVENT_NOTE_OFF;
        ev.note.header.flags    = 0;
        ev.note.note_id         = note_id;
        ev.note.port_index      = port;
        ev.note.channel         = channel;
        ev.note.key             = key;
        ev.note.velocity        = velocity;
        return ev;
    }

} // namespace

rt_control_thread::rt_control_thread(config cfg, param_queue& queue, input_event_queue& in_queue)
    : cfg_(std::move(cfg)), queue_(queue), in_queue_(in_queue) {
}

rt_control_thread::~rt_control_thread() {
    stop();
}

void rt_control_thread::start() {
    listen_fd_ = make_listen_socket(cfg_.socket_path);
    if (listen_fd_ < 0)
        return;

        // Auto-load LuaJIT + Fennel when the caller has not provided one.
        // Requires LuaJIT at link time (NOMOS_RT_HAS_LUAJIT); no-op in stub builds.
#ifdef NOMOS_RT_HAS_LUAJIT
    if (!cfg_.lua) {
        lua_owned_ = luajit_participant::load();
        cfg_.lua   = lua_owned_.get();
    }
#endif

    // Wire the WASM hot-swap Lua C binding now that cfg_.lua is resolved.
    if (cfg_.lua && cfg_.wasm_swap_fn)
        cfg_.lua->register_wasm_swap_fn(cfg_.wasm_swap_fn);

    running_.store(true, std::memory_order_release);
    thread_ = std::thread(&rt_control_thread::run, this);
}

void rt_control_thread::set_wasm_swap_fn(
    std::function<bool(std::string_view, std::string_view)> fn) {
    cfg_.wasm_swap_fn = std::move(fn);
}

void rt_control_thread::stop() {
    if (!running_.exchange(false, std::memory_order_acq_rel))
        return;

    // Unblock any read_message() call on an established connection immediately.
    // Without this, stop() would stall until the client sends or disconnects.
    {
        std::lock_guard<std::mutex> lock(write_mutex_);
        if (conn_fd_write_ >= 0)
            ::shutdown(conn_fd_write_, SHUT_RDWR);
    }

    // Unblock accept() and remove the socket file.
    if (listen_fd_ >= 0) {
        ::shutdown(listen_fd_, SHUT_RDWR);
        ::close(listen_fd_);
        listen_fd_ = -1;
        ::unlink(cfg_.socket_path.c_str());
    }

    if (thread_.joinable())
        thread_.join();
}

bool rt_control_thread::running() const noexcept {
    return running_.load(std::memory_order_acquire);
}

void rt_control_thread::run() {
    while (running_.load(std::memory_order_acquire)) {
        const int conn_fd = ::accept(listen_fd_, nullptr, nullptr);
        if (conn_fd < 0) {
            if (errno == EINTR)
                continue; // retry on signal interruption
            break;
        }

        handle_connection(conn_fd);
        ::close(conn_fd);
    }
}

void rt_control_thread::push_frame(uint8_t type, std::string_view payload) {
    std::lock_guard<std::mutex> lock(write_mutex_);
    if (conn_fd_write_ < 0)
        return;
    ipc::write_message(conn_fd_write_, type, payload);
}

void rt_control_thread::handle_connection(int conn_fd) {
    std::fprintf(stderr, "[ipc] client connected fd=%d\n", conn_fd);
    {
        std::lock_guard<std::mutex> lock(write_mutex_);
        conn_fd_write_ = conn_fd;
    }

    std::optional<session> sess;

    while (running_.load(std::memory_order_acquire)) {
        auto result = ipc::read_message(conn_fd);
        if (!result)
            break;
        dispatch_message(conn_fd, *result, sess);
    }

    {
        std::lock_guard<std::mutex> lock(write_mutex_);
        conn_fd_write_ = -1;
    }
    std::fprintf(stderr, "[ipc] client disconnected fd=%d\n", conn_fd);
}

void rt_control_thread::dispatch_message(int conn_fd, const ipc::message& msg,
                                         std::optional<session>& sess) {
    switch (msg.type()) {
    case ipc::msg_session_open: {
        sess = session::open(cfg_.db_path);
        break;
    }

    case ipc::msg_session_close: {
        if (sess)
            sess->close();
        sess.reset();
        break;
    }

    case ipc::msg_register_source: {
        if (!sess || msg.payload.empty())
            break;
        const std::string_view text{reinterpret_cast<const char*>(msg.payload.data()),
                                    msg.payload.size()};
        auto                   parsed = edn::parse(text);
        if (!parsed || !parsed->is<edn::map>())
            break;
        const auto& m   = parsed->get<edn::map>();
        const auto* id  = m.find_kw("id");
        const auto* nm  = m.find_kw("name");
        const auto* dsc = m.find_kw("description");
        if (!id || !id->is<edn::keyword>())
            break;
        sess->register_source({
            .id          = id->get<edn::keyword>(),
            .name        = nm && nm->is<std::string>() ? nm->get<std::string>() : "",
            .description = dsc && dsc->is<std::string>() ? dsc->get<std::string>() : "",
        });
        break;
    }

    case ipc::msg_param_set: {
        if (msg.payload.empty())
            break;
        const std::string_view text{reinterpret_cast<const char*>(msg.payload.data()),
                                    msg.payload.size()};
        auto                   parsed = edn::parse(text);
        if (!parsed || !parsed->is<edn::map>())
            break;
        const auto& m     = parsed->get<edn::map>();
        const auto* path  = m.find_kw("path");
        const auto* value = m.find_kw("value");
        if (!path || !value)
            break;
        queue_.push(param_event{.path = *path, .value = *value, .time = {}});
        break;
    }

    case ipc::msg_note_on:
    case ipc::msg_note_off: {
        if (msg.payload.empty())
            break;
        const std::string_view text{reinterpret_cast<const char*>(msg.payload.data()),
                                    msg.payload.size()};
        auto                   parsed = edn::parse(text);
        if (!parsed || !parsed->is<edn::map>())
            break;
        const auto& m = parsed->get<edn::map>();

        auto get_i16 = [&](const char* kw, int16_t def) -> int16_t {
            const auto* v = m.find_kw(kw);
            if (v && v->is<int64_t>())
                return static_cast<int16_t>(v->get<int64_t>());
            return def;
        };
        auto get_dbl = [&](const char* kw, double def) -> double {
            const auto* v = m.find_kw(kw);
            if (v && v->is<double>())
                return v->get<double>();
            if (v && v->is<int64_t>())
                return static_cast<double>(v->get<int64_t>());
            return def;
        };

        const bool is_on = (msg.type() == ipc::msg_note_on);
        auto       ev =
            make_note_event(is_on, get_i16("key", 60), get_i16("channel", 0), get_i16("port", 0),
                            get_i16("note-id", -1), get_dbl("velocity", 0.0));

        // Optional :beat field — if present and a scheduler is wired, defer the
        // event until that Link beat rather than dispatching immediately.
        const auto* beat_v = m.find_kw("beat");
        if (beat_v && cfg_.sched_staging) {
            const double target = get_dbl("beat", 0.0);
            cfg_.sched_staging->push(scheduled_event{.beat = target, .event = ev});
        } else {
            in_queue_.push(ev);
        }
        break;
    }

    case ipc::msg_schedule_bundle: {
        // Bundle of beat-accurate events: {:at-beat D :events [{:at-tick N :type
        // :kw ...}]} Each event's beat = at-beat + at-tick / 24.0. Requires
        // sched_staging to be wired; silently drops the bundle otherwise.
        if (msg.payload.empty() || !cfg_.sched_staging)
            break;
        const std::string_view text{reinterpret_cast<const char*>(msg.payload.data()),
                                    msg.payload.size()};
        auto                   parsed = edn::parse(text);
        if (!parsed || !parsed->is<edn::map>())
            break;
        const auto& m        = parsed->get<edn::map>();
        const auto* beat_v   = m.find_kw("at-beat");
        const auto* events_v = m.find_kw("events");
        if (!beat_v || !events_v || !events_v->is<edn::vector>())
            break;

        double anchor = 0.0;
        if (beat_v->is<double>())
            anchor = beat_v->get<double>();
        else if (beat_v->is<int64_t>())
            anchor = static_cast<double>(beat_v->get<int64_t>());

        for (const auto& item : events_v->get<edn::vector>().items) {
            if (!item.is<edn::map>())
                continue;
            const auto& em = item.get<edn::map>();

            auto get_i = [&](const char* kw, int64_t def) -> int64_t {
                const auto* v = em.find_kw(kw);
                if (v && v->is<int64_t>())
                    return v->get<int64_t>();
                return def;
            };
            auto get_d = [&](const char* kw, double def) -> double {
                const auto* v = em.find_kw(kw);
                if (v && v->is<double>())
                    return v->get<double>();
                if (v && v->is<int64_t>())
                    return static_cast<double>(v->get<int64_t>());
                return def;
            };

            const int64_t at_tick = get_i("at-tick", 0);
            const double  beat    = anchor + at_tick / 24.0;

            const auto* type_v = em.find_kw("type");
            const bool  is_on  = !(type_v && type_v->is<edn::keyword>() &&
                                 type_v->get<edn::keyword>().name == "note-off");

            auto ev = make_note_event(
                is_on, static_cast<int16_t>(get_i("key", 60)),
                static_cast<int16_t>(get_i("channel", 0)), static_cast<int16_t>(get_i("port", 0)),
                static_cast<int16_t>(get_i("note-id", -1)), get_d("velocity", 0.0));

            cfg_.sched_staging->push(scheduled_event{.beat = beat, .event = ev});
        }
        break;
    }

    case ipc::msg_midi_in: {
        if (msg.payload.empty())
            break;
        const std::string_view text{reinterpret_cast<const char*>(msg.payload.data()),
                                    msg.payload.size()};
        auto                   parsed = edn::parse(text);
        if (!parsed || !parsed->is<edn::map>())
            break;
        const auto& m = parsed->get<edn::map>();

        const auto* port_v = m.find_kw("port");
        const auto* data_v = m.find_kw("data");
        if (!data_v || !data_v->is<edn::vector>())
            break;
        const auto& bytes = data_v->get<edn::vector>().items;

        clap_event_union ev{};
        ev.midi.header.size     = sizeof(clap_event_midi_t);
        ev.midi.header.time     = 0;
        ev.midi.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        ev.midi.header.type     = CLAP_EVENT_MIDI;
        ev.midi.header.flags    = 0;
        ev.midi.port_index =
            (port_v && port_v->is<int64_t>()) ? static_cast<uint16_t>(port_v->get<int64_t>()) : 0;
        for (std::size_t i = 0; i < 3 && i < bytes.size(); ++i) {
            if (bytes[i].is<int64_t>())
                ev.midi.data[i] = static_cast<uint8_t>(bytes[i].get<int64_t>());
        }
        in_queue_.push(ev);
        break;
    }

    case ipc::msg_tx_log: {
        if (!sess || msg.payload.empty())
            break;
        const std::string_view text{reinterpret_cast<const char*>(msg.payload.data()),
                                    msg.payload.size()};
        auto                   parsed = edn::parse(text);
        if (!parsed || !parsed->is<edn::map>())
            break;
        const auto& m = parsed->get<edn::map>();

        txlog::entry e;

        if (const auto* v = m.find_kw("id"); v && v->is<edn::tagged>()) {
            const auto& t = v->get<edn::tagged>();
            if (t.tag == "uuid" && t.val && t.val->is<std::string>())
                e.id = parse_uuid(t.val->get<std::string>());
        }

        if (const auto* v = m.find_kw("beat"); v) {
            if (v->is<double>())
                e.beat = v->get<double>();
            else if (v->is<int64_t>())
                e.beat = static_cast<double>(v->get<int64_t>());
        }

        if (const auto* v = m.find_kw("wall-ns"); v && v->is<int64_t>())
            e.wall_ns = v->get<int64_t>();

        if (const auto* v = m.find_kw("source"); v && v->is<edn::keyword>())
            e.source = v->get<edn::keyword>();

        if (const auto* v = m.find_kw("path"); v)
            e.path = *v;

        if (const auto* v = m.find_kw("before"); v && !v->is_nil())
            e.before = *v;
        if (const auto* v = m.find_kw("after"); v && !v->is_nil())
            e.after = *v;
        if (const auto* v = m.find_kw("parent"); v && !v->is_nil())
            e.parent = *v;

        sess->emit(e);
        break;
    }

    case ipc::msg_modulator_start: {
        // {:id :kw :type :slope :rate 1.0 :shape 0 :slope 0 :smoothness 0 :depth 1
        // :bipolar 1}
        if (!cfg_.mod_engine || msg.payload.empty())
            break;
        const std::string_view text{reinterpret_cast<const char*>(msg.payload.data()),
                                    msg.payload.size()};
        auto                   parsed = edn::parse(text);
        if (!parsed || !parsed->is<edn::map>())
            break;
        const auto& m = parsed->get<edn::map>();

        const auto* id_v   = m.find_kw("id");
        const auto* type_v = m.find_kw("type");
        if (!id_v || !type_v)
            break;

        // Build modulator id string.
        std::string id;
        if (id_v->is<edn::keyword>())
            id = id_v->get<edn::keyword>().name;
        else if (id_v->is<std::string>())
            id = id_v->get<std::string>();
        if (id.empty())
            break;

        std::string type_name;
        if (type_v->is<edn::keyword>())
            type_name = type_v->get<edn::keyword>().name;

        auto get_f = [&](const char* kw, float def) -> float {
            const auto* v = m.find_kw(kw);
            if (!v)
                return def;
            if (v->is<double>())
                return static_cast<float>(v->get<double>());
            if (v->is<int64_t>())
                return static_cast<float>(v->get<int64_t>());
            return def;
        };

        if (type_name == "slope") {
            auto mod = std::make_unique<slope_modulator>();
            mod->update("rate", get_f("rate", 1.0f));
            mod->update("shape", get_f("shape", 0.0f));
            mod->update("slope", get_f("slope", 0.0f));
            mod->update("smoothness", get_f("smoothness", 0.0f));
            mod->update("depth", get_f("depth", 1.0f));
            mod->update("bipolar", get_f("bipolar", 1.0f));
            cfg_.mod_engine->start(std::move(id), std::move(mod));

        } else if (type_name == "segment") {
            // {:id :kw :type :segment
            //  :segments [{:type :ramp :primary 0.5 :secondary 0.5 :loop false} …]}
            const auto* segs_v = m.find_kw("segments");
            if (!segs_v || !segs_v->is<edn::vector>())
                break;

            static const std::unordered_map<std::string, segment_modulator::type> type_map{
                {"ramp", segment_modulator::type::ramp},
                {"step", segment_modulator::type::step},
                {"hold", segment_modulator::type::hold},
                {"alt", segment_modulator::type::alt},
            };

            std::vector<segment_modulator::segment_def> defs;
            for (const auto& item : segs_v->get<edn::vector>().items) {
                if (!item.is<edn::map>())
                    continue;
                const auto& sm = item.get<edn::map>();

                segment_modulator::segment_def def;

                if (const auto* t = sm.find_kw("type"); t && t->is<edn::keyword>()) {
                    auto it = type_map.find(std::string{t->get<edn::keyword>().name});
                    if (it != type_map.end())
                        def.kind = it->second;
                }

                auto get_sf = [&](const char* kw, float d) -> float {
                    const auto* v = sm.find_kw(kw);
                    if (!v)
                        return d;
                    if (v->is<double>())
                        return static_cast<float>(v->get<double>());
                    if (v->is<int64_t>())
                        return static_cast<float>(v->get<int64_t>());
                    return d;
                };

                def.primary   = get_sf("primary", 0.5f);
                def.secondary = get_sf("secondary", 0.5f);

                if (const auto* l = sm.find_kw("loop"); l)
                    def.loop = !(l->is_nil() || (l->is<bool>() && !l->get<bool>()));

                defs.push_back(def);
            }

            if (!defs.empty())
                cfg_.mod_engine->start(std::move(id),
                                       std::make_unique<segment_modulator>(std::span{defs}));

        } else if (type_name == "slew") {
            auto mod = std::make_unique<slew_modulator>();
            mod->update("rise", get_f("rise", 0.1f));
            mod->update("fall", get_f("fall", 0.1f));
            mod->update("cycle", get_f("cycle", 0.0f));
            mod->update("depth", get_f("depth", 1.0f));
            cfg_.mod_engine->start(std::move(id), std::move(mod));

        } else if (type_name == "shift-register") {
            // :mode keyword selects feedback algorithm; length and dac_bits are
            // constructor-fixed so they can't be updated live.
            static const std::unordered_map<std::string, shift_register_modulator::mode> mode_map{
                {"lfsr", shift_register_modulator::mode::lfsr},
                {"rungler", shift_register_modulator::mode::rungler},
                {"turing", shift_register_modulator::mode::turing},
                {"open", shift_register_modulator::mode::open},
            };
            shift_register_modulator::mode sr_mode = shift_register_modulator::mode::turing;
            if (const auto* mv = m.find_kw("mode"); mv && mv->is<edn::keyword>()) {
                auto it = mode_map.find(std::string{mv->get<edn::keyword>().name});
                if (it != mode_map.end())
                    sr_mode = it->second;
            }
            const int length   = static_cast<int>(get_f("length", 16.0f));
            const int dac_bits = static_cast<int>(get_f("dac_bits", 3.0f));
            auto      mod = std::make_unique<shift_register_modulator>(sr_mode, length, dac_bits);
            mod->update("clock_rate", get_f("clock_rate", 2.0f));
            mod->update("data", get_f("data", 0.5f));
            mod->update("param", get_f("param", 0.5f));
            mod->update("depth", get_f("depth", 1.0f));
            cfg_.mod_engine->start(std::move(id), std::move(mod));

        } else if (type_name == "fractal") {
            auto mod = std::make_unique<fractal_modulator>();
            mod->update("base_rate", get_f("base_rate", 0.1f));
            mod->update("octaves", get_f("octaves", 4.0f));
            mod->update("lacunarity", get_f("lacunarity", 2.0f));
            mod->update("persistence", get_f("persistence", 0.5f));
            mod->update("depth", get_f("depth", 1.0f));
            cfg_.mod_engine->start(std::move(id), std::move(mod));

        } else if (type_name == "stochastic") {
            auto mod = std::make_unique<stochastic_modulator>();
            mod->update("rate", get_f("rate", 2.0f));
            mod->update("bias", get_f("bias", 0.5f));
            mod->update("spread", get_f("spread", 0.5f));
            mod->update("deja_vu", get_f("deja_vu", 0.0f));
            mod->update("length", get_f("length", 8.0f));
            mod->update("depth", get_f("depth", 1.0f));
            cfg_.mod_engine->start(std::move(id), std::move(mod));

        } else if (type_name == "graph") {
            const auto* graph_v = m.find_kw("graph");
            if (!graph_v)
                break;

            std::unordered_map<std::string, float> params;
            if (const auto* pv = m.find_kw("params"); pv && pv->is<edn::map>()) {
                for (const auto& [k, v] : pv->get<edn::map>().entries) {
                    if (!k.is<edn::keyword>())
                        continue;
                    float fv = 0.0f;
                    if (v.is<double>())
                        fv = static_cast<float>(v.get<double>());
                    else if (v.is<int64_t>())
                        fv = static_cast<float>(v.get<int64_t>());
                    else
                        continue;
                    params[std::string(k.get<edn::keyword>().name)] = fv;
                }
            }

            cfg_.mod_engine->start(
                std::move(id),
                std::make_unique<graph_modulator>(parse_control_graph(*graph_v, std::move(params)),
                                                  cfg_.mod_engine));

        } else if (type_name == "cv-channel-decoder") {
            // {:id :kw :type :cv-channel-decoder
            //  :span [:source-id]          ; or [:source-id :aux] / [:source-id
            //  :gate] :channels 1                 ; 1..8 :space 1.0}
            int channels = 1;
            if (const auto* v = m.find_kw("channels")) {
                if (v->is<int64_t>())
                    channels = static_cast<int>(v->get<int64_t>());
                else if (v->is<double>())
                    channels = static_cast<int>(v->get<double>());
            }

            std::string                      source_id;
            cv_channel_decoder::source_field field = cv_channel_decoder::source_field::cv;

            if (const auto* sv = m.find_kw("span")) {
                if (sv->is<edn::vector>()) {
                    const auto& vec = sv->get<edn::vector>().items;
                    if (!vec.empty() && vec[0].is<edn::keyword>())
                        source_id = std::string{vec[0].get<edn::keyword>().name};
                    if (vec.size() >= 2 && vec[1].is<edn::keyword>()) {
                        const auto fn = vec[1].get<edn::keyword>().name;
                        if (fn == "aux")
                            field = cv_channel_decoder::source_field::aux;
                        if (fn == "gate")
                            field = cv_channel_decoder::source_field::gate;
                    }
                }
            }

            auto mod = std::make_unique<cv_channel_decoder>(channels, cfg_.mod_engine,
                                                            std::move(source_id), field);
            mod->update("space", get_f("space", 1.0f));
            mod->update("clocked", get_f("clocked", 0.0f));

            // Static span value (used when no source_id / as initial value).
            if (const auto* sv = m.find_kw("span"); sv && !sv->is<edn::vector>()) {
                if (sv->is<double>())
                    mod->update("span", static_cast<float>(sv->get<double>()));
                else if (sv->is<int64_t>())
                    mod->update("span", static_cast<float>(sv->get<int64_t>()));
            }

            cfg_.mod_engine->start(std::move(id), std::move(mod));

        } else if (type_name == "divine-cmos") {
            // {:id :kw :type :divine-cmos
            //  :clock1 [:src-id]  :clock2 [:src-id]
            //  :gains [g0 g1 g2 g3]  :slew 0.0}
            auto get_src = [&](const char* kw) -> std::string {
                const auto* v = m.find_kw(kw);
                if (!v || !v->is<edn::vector>())
                    return {};
                const auto& vec = v->get<edn::vector>().items;
                if (!vec.empty() && vec[0].is<edn::keyword>())
                    return std::string{vec[0].get<edn::keyword>().name};
                return {};
            };
            auto mod = std::make_unique<divine_cmos_modulator>(cfg_.mod_engine, get_src("clock1"),
                                                               get_src("clock2"));
            if (const auto* gv = m.find_kw("gains"); gv && gv->is<edn::vector>()) {
                const auto& items = gv->get<edn::vector>().items;
                for (int i = 0; i < 4 && i < static_cast<int>(items.size()); ++i) {
                    float g = 1.0f;
                    if (items[i].is<double>())
                        g = static_cast<float>(items[i].get<double>());
                    else if (items[i].is<int64_t>())
                        g = static_cast<float>(items[i].get<int64_t>());
                    mod->update(std::string{"gain"} + char('0' + i), g);
                }
            }
            mod->update("slew", get_f("slew", 0.0f));
            cfg_.mod_engine->start(std::move(id), std::move(mod));

        } else if (type_name == "statues") {
            // {:id :kw :type :statues
            //  :in [:src-id]  :addr [:src-id]}
            auto get_src = [&](const char* kw) -> std::string {
                const auto* v = m.find_kw(kw);
                if (!v || !v->is<edn::vector>())
                    return {};
                const auto& vec = v->get<edn::vector>().items;
                if (!vec.empty() && vec[0].is<edn::keyword>())
                    return std::string{vec[0].get<edn::keyword>().name};
                return {};
            };
            auto mod = std::make_unique<statues_modulator>(cfg_.mod_engine, get_src("in"),
                                                           get_src("addr"));
            cfg_.mod_engine->start(std::move(id), std::move(mod));

        } else if (type_name == "cipher") {
            // {:id :kw :type :cipher
            //  :clock [:src] :data1 [:src] :data2 [:src] :strobe [:src]}
            auto get_src = [&](const char* kw) -> std::string {
                const auto* v = m.find_kw(kw);
                if (!v || !v->is<edn::vector>())
                    return {};
                const auto& vec = v->get<edn::vector>().items;
                if (!vec.empty() && vec[0].is<edn::keyword>())
                    return std::string{vec[0].get<edn::keyword>().name};
                return {};
            };
            auto mod = std::make_unique<cipher_modulator>(cfg_.mod_engine, get_src("clock"),
                                                          get_src("data1"), get_src("data2"),
                                                          get_src("strobe"));
            cfg_.mod_engine->start(std::move(id), std::move(mod));

        } else if (type_name == "bools-ring") {
            // {:id :kw :type :bools-ring
            //  :mode :xor  :slew 0.0
            //  :ins {:in0 [:src] :in1 [:src] :in2 [:src] :in3 [:src]}}
            static const std::unordered_map<std::string, bools_ring_modulator::mode> mode_map{
                {"xor", bools_ring_modulator::mode::xor_mode},
                {"or", bools_ring_modulator::mode::or_mode},
                {"and", bools_ring_modulator::mode::and_mode},
                {"nor", bools_ring_modulator::mode::nor_mode},
                {"nand", bools_ring_modulator::mode::nand_mode},
                {"xnor", bools_ring_modulator::mode::xnor_mode},
            };
            auto br_mode = bools_ring_modulator::mode::xor_mode;
            if (const auto* mv = m.find_kw("mode"); mv && mv->is<edn::keyword>()) {
                auto it = mode_map.find(std::string{mv->get<edn::keyword>().name});
                if (it != mode_map.end())
                    br_mode = it->second;
            }

            // Parse :ins sub-map for per-input source IDs.
            auto get_in_src = [&](const char* kw) -> std::string {
                const auto* ins_v = m.find_kw("ins");
                if (!ins_v || !ins_v->is<edn::map>())
                    return {};
                const auto* v = ins_v->get<edn::map>().find_kw(kw);
                if (!v || !v->is<edn::vector>())
                    return {};
                const auto& vec = v->get<edn::vector>().items;
                if (!vec.empty() && vec[0].is<edn::keyword>())
                    return std::string{vec[0].get<edn::keyword>().name};
                return {};
            };

            auto mod = std::make_unique<bools_ring_modulator>(
                br_mode, cfg_.mod_engine, get_in_src("in0"), get_in_src("in1"), get_in_src("in2"),
                get_in_src("in3"), /*sample_src=*/"");
            mod->update("slew", get_f("slew", 0.0f));
            cfg_.mod_engine->start(std::move(id), std::move(mod));

        } else if (type_name == "sloth-chaos") {
            // {:id :kw :type :sloth-chaos
            //  :variant :torpor|:apathy|:inertia|:triple
            //  :knob 0.5  :cv [:src]}
            static const std::unordered_map<std::string, sloth_chaos_modulator::variant>
                variant_map{
                    {"torpor", sloth_chaos_modulator::variant::torpor},
                    {"apathy", sloth_chaos_modulator::variant::apathy},
                    {"inertia", sloth_chaos_modulator::variant::inertia},
                    {"triple", sloth_chaos_modulator::variant::triple},
                };
            auto v = sloth_chaos_modulator::variant::torpor;
            if (const auto* vv = m.find_kw("variant"); vv && vv->is<edn::keyword>()) {
                auto it = variant_map.find(std::string{vv->get<edn::keyword>().name});
                if (it != variant_map.end())
                    v = it->second;
            }
            auto get_sloth_src = [&](const char* kw) -> std::string {
                const auto* v = m.find_kw(kw);
                if (!v || !v->is<edn::vector>())
                    return {};
                const auto& vec = v->get<edn::vector>().items;
                if (!vec.empty() && vec[0].is<edn::keyword>())
                    return std::string{vec[0].get<edn::keyword>().name};
                return {};
            };
            auto mod =
                std::make_unique<sloth_chaos_modulator>(v, cfg_.mod_engine, get_sloth_src("cv"));
            mod->update("knob", get_f("knob", 0.5f));
            cfg_.mod_engine->start(std::move(id), std::move(mod));

        } else if (type_name == "squid-axon") {
            // {:id :kw :type :squid-axon
            //  :clock [:src]  :in1 [:src]  :in2 [:src]  :in3 [:src]
            //  :nl-fb 0.0  :lin-fb 0.0}
            auto get_squid_src = [&](const char* kw) -> std::string {
                const auto* v = m.find_kw(kw);
                if (!v || !v->is<edn::vector>())
                    return {};
                const auto& vec = v->get<edn::vector>().items;
                if (!vec.empty() && vec[0].is<edn::keyword>())
                    return std::string{vec[0].get<edn::keyword>().name};
                return {};
            };
            auto mod = std::make_unique<squid_axon_modulator>(
                cfg_.mod_engine, get_squid_src("clock"), get_squid_src("in1"), get_squid_src("in2"),
                get_squid_src("in3"));
            mod->update("nl_fb", get_f("nl-fb", 0.0f));
            mod->update("lin_fb", get_f("lin-fb", 0.0f));
            if (m.find_kw("in3"))
                mod->update("in3_patched", 1.0f);
            cfg_.mod_engine->start(std::move(id), std::move(mod));

        } else if (type_name == "genie") {
            // {:id :kw :type :genie
            //  :n 3  :gains [g0 g1 g2]  :sense [s0 s1 s2]  :response [r0 r1 r2]}
            const int n_neurons = static_cast<int>(get_f("n", 3.0f));

            auto mod = std::make_unique<genie_modulator>(n_neurons, cfg_.mod_engine);

            // Parse per-neuron vector params.
            auto apply_vec_param = [&](const char* kw, const char* prefix) {
                const auto* vv = m.find_kw(kw);
                if (!vv || !vv->is<edn::vector>())
                    return;
                const auto& items = vv->get<edn::vector>().items;
                for (int i = 0; i < (int)items.size() && i < genie_modulator::kMaxN; ++i) {
                    if (items[i].is<double>()) {
                        mod->update(std::string(prefix) + char('0' + i),
                                    static_cast<float>(items[i].get<double>()));
                    }
                }
            };
            apply_vec_param("gains", "gain");
            apply_vec_param("sense", "sense");
            apply_vec_param("response", "response");
            cfg_.mod_engine->start(std::move(id), std::move(mod));

        } else if (type_name == "lets-splosh") {
            // {:id :kw :type :lets-splosh
            //  :ins {:c [:src] :t [:src] :n [:src] :b [:src]}}
            auto get_ins_src = [&](const char* kw) -> std::string {
                const auto* ins_v = m.find_kw("ins");
                if (!ins_v || !ins_v->is<edn::map>())
                    return {};
                const auto* v = ins_v->get<edn::map>().find_kw(kw);
                if (!v || !v->is<edn::vector>())
                    return {};
                const auto& vec = v->get<edn::vector>().items;
                if (!vec.empty() && vec[0].is<edn::keyword>())
                    return std::string{vec[0].get<edn::keyword>().name};
                return {};
            };
            auto mod = std::make_unique<lets_splosh_modulator>(cfg_.mod_engine, get_ins_src("c"),
                                                               get_ins_src("t"), get_ins_src("n"),
                                                               get_ins_src("b"));
            cfg_.mod_engine->start(std::move(id), std::move(mod));

        } else if (cfg_.ext_registry) {
            // Extension type: factory constructs, then float params are applied
            // generically.
            auto mod = cfg_.ext_registry->make(type_name);
            if (mod) {
                for (const auto& [k, v] : m.entries) {
                    if (!k.is<edn::keyword>())
                        continue;
                    const std::string_view key = k.get<edn::keyword>().name;
                    if (key == "id" || key == "type")
                        continue;
                    float fv = 0.0f;
                    if (v.is<double>())
                        fv = static_cast<float>(v.get<double>());
                    else if (v.is<int64_t>())
                        fv = static_cast<float>(v.get<int64_t>());
                    else
                        continue;
                    mod->update(key, fv);
                }
                cfg_.mod_engine->start(std::move(id), std::move(mod));
            } else {
                fprintf(stderr, "nomos-rt: msg_modulator_start: unrecognised type '%.*s'\n",
                        static_cast<int>(type_name.size()), type_name.data());
            }
        } else {
            fprintf(stderr, "nomos-rt: msg_modulator_start: unrecognised type '%.*s'\n",
                    static_cast<int>(type_name.size()), type_name.data());
        }
        break;
    }

    case ipc::msg_modulator_stop: {
        // {:id :kw}
        if (!cfg_.mod_engine || msg.payload.empty())
            break;
        const std::string_view text{reinterpret_cast<const char*>(msg.payload.data()),
                                    msg.payload.size()};
        auto                   parsed = edn::parse(text);
        if (!parsed || !parsed->is<edn::map>())
            break;
        const auto& m    = parsed->get<edn::map>();
        const auto* id_v = m.find_kw("id");
        if (!id_v)
            break;

        std::string id;
        if (id_v->is<edn::keyword>())
            id = id_v->get<edn::keyword>().name;
        else if (id_v->is<std::string>())
            id = id_v->get<std::string>();
        if (!id.empty())
            cfg_.mod_engine->stop(id);
        break;
    }

    case ipc::msg_modulator_update: {
        // {:id :kw :key "param-name" :value 0.5}
        if (!cfg_.mod_engine || msg.payload.empty())
            break;
        const std::string_view text{reinterpret_cast<const char*>(msg.payload.data()),
                                    msg.payload.size()};
        auto                   parsed = edn::parse(text);
        if (!parsed || !parsed->is<edn::map>())
            break;
        const auto& m = parsed->get<edn::map>();

        const auto* id_v    = m.find_kw("id");
        const auto* key_v   = m.find_kw("key");
        const auto* value_v = m.find_kw("value");
        if (!id_v || !key_v || !value_v)
            break;

        std::string id;
        if (id_v->is<edn::keyword>())
            id = id_v->get<edn::keyword>().name;
        else if (id_v->is<std::string>())
            id = id_v->get<std::string>();
        if (id.empty())
            break;

        std::string key;
        if (key_v->is<std::string>())
            key = key_v->get<std::string>();
        else if (key_v->is<edn::keyword>())
            key = key_v->get<edn::keyword>().name;
        if (key.empty())
            break;

        float value = 0.0f;
        if (value_v->is<double>())
            value = static_cast<float>(value_v->get<double>());
        else if (value_v->is<int64_t>())
            value = static_cast<float>(value_v->get<int64_t>());

        cfg_.mod_engine->update_param(id, key, value);
        break;
    }

    case ipc::msg_cc: {
        // {:port N :channel N :cc N :value N}  — channel 0-15, cc 0-127, value
        // 0-127
        if (!cfg_.midi || msg.payload.empty())
            break;
        const std::string_view text{reinterpret_cast<const char*>(msg.payload.data()),
                                    msg.payload.size()};
        auto                   parsed = edn::parse(text);
        if (!parsed || !parsed->is<edn::map>())
            break;
        const auto& m      = parsed->get<edn::map>();
        auto        get_u8 = [&](const char* kw, uint8_t def) -> uint8_t {
            const auto* v = m.find_kw(kw);
            return (v && v->is<int64_t>()) ? static_cast<uint8_t>(v->get<int64_t>()) : def;
        };
        const uint8_t ch  = get_u8("channel", 0) & 0x0F;
        const uint8_t cc  = get_u8("cc", 0);
        const uint8_t val = get_u8("value", 0);
        cfg_.midi->send({static_cast<uint8_t>(0xB0 | ch), cc, val});
        break;
    }

    case ipc::msg_pitch_bend: {
        // {:port N :channel N :value N}  — value 0-16383, centre 8192
        if (!cfg_.midi || msg.payload.empty())
            break;
        const std::string_view text{reinterpret_cast<const char*>(msg.payload.data()),
                                    msg.payload.size()};
        auto                   parsed = edn::parse(text);
        if (!parsed || !parsed->is<edn::map>())
            break;
        const auto& m     = parsed->get<edn::map>();
        auto        get_i = [&](const char* kw, int64_t def) -> int64_t {
            const auto* v = m.find_kw(kw);
            return (v && v->is<int64_t>()) ? v->get<int64_t>() : def;
        };
        const uint8_t ch  = static_cast<uint8_t>(get_i("channel", 0) & 0x0F);
        const int64_t raw = std::max(INT64_C(0), std::min(INT64_C(16383), get_i("value", 8192)));
        cfg_.midi->send({static_cast<uint8_t>(0xE0 | ch), static_cast<uint8_t>(raw & 0x7F),
                         static_cast<uint8_t>((raw >> 7) & 0x7F)});
        break;
    }

    case ipc::msg_chan_pressure: {
        // {:port N :channel N :value N}  — value 0-127
        if (!cfg_.midi || msg.payload.empty())
            break;
        const std::string_view text{reinterpret_cast<const char*>(msg.payload.data()),
                                    msg.payload.size()};
        auto                   parsed = edn::parse(text);
        if (!parsed || !parsed->is<edn::map>())
            break;
        const auto& m      = parsed->get<edn::map>();
        auto        get_u8 = [&](const char* kw, uint8_t def) -> uint8_t {
            const auto* v = m.find_kw(kw);
            return (v && v->is<int64_t>()) ? static_cast<uint8_t>(v->get<int64_t>()) : def;
        };
        const uint8_t ch  = get_u8("channel", 0) & 0x0F;
        const uint8_t val = get_u8("value", 0);
        cfg_.midi->send({static_cast<uint8_t>(0xD0 | ch), val});
        break;
    }

    case ipc::msg_sysex: {
        // {:port N :data [b0 b1 … bN]}  — raw SysEx bytes including F0 and F7
        if (!cfg_.midi || msg.payload.empty())
            break;
        const std::string_view text{reinterpret_cast<const char*>(msg.payload.data()),
                                    msg.payload.size()};
        auto                   parsed = edn::parse(text);
        if (!parsed || !parsed->is<edn::map>())
            break;
        const auto& m      = parsed->get<edn::map>();
        const auto* data_v = m.find_kw("data");
        if (!data_v || !data_v->is<edn::vector>())
            break;
        std::vector<uint8_t> bytes;
        bytes.reserve(data_v->get<edn::vector>().items.size());
        for (const auto& item : data_v->get<edn::vector>().items) {
            if (item.is<int64_t>())
                bytes.push_back(static_cast<uint8_t>(item.get<int64_t>() & 0xFF));
        }
        if (!bytes.empty())
            cfg_.midi->send(bytes);
        break;
    }

    case ipc::msg_mts: {
        // {:port N :tuning {midi→hz} :tuning-prog N :device-id N|:all}
        // Assembles and sends a 408-byte MTS Bulk Dump SysEx.
        if (!cfg_.midi || msg.payload.empty())
            break;
        const std::string_view text{reinterpret_cast<const char*>(msg.payload.data()),
                                    msg.payload.size()};
        auto                   parsed = edn::parse(text);
        if (!parsed || !parsed->is<edn::map>())
            break;
        const auto& m        = parsed->get<edn::map>();
        const auto* tuning_v = m.find_kw("tuning");
        if (!tuning_v || !tuning_v->is<edn::map>())
            break;

        // Build key→Hz lookup from the EDN map
        std::unordered_map<int, double> tuning;
        tuning.reserve(128);
        for (const auto& [k, v] : tuning_v->get<edn::map>().entries) {
            if (!k.is<int64_t>())
                continue;
            double hz = 0.0;
            if (v.is<double>())
                hz = v.get<double>();
            else if (v.is<int64_t>())
                hz = static_cast<double>(v.get<int64_t>());
            tuning[static_cast<int>(k.get<int64_t>())] = hz;
        }

        const auto*   prog_v = m.find_kw("tuning-prog");
        const uint8_t prog   = (prog_v && prog_v->is<int64_t>())
                                   ? static_cast<uint8_t>(prog_v->get<int64_t>() & 0x7F)
                                   : 0;

        const auto* dev_v     = m.find_kw("device-id");
        uint8_t     device_id = 0x7F; // :all
        if (dev_v && dev_v->is<int64_t>())
            device_id = static_cast<uint8_t>(dev_v->get<int64_t>() & 0x7F);

        cfg_.midi->send(mts_bulk_dump(tuning, prog, device_id));
        break;
    }

    case ipc::msg_repl_eval: {
        // {:dest :fennel|:nous|:vcvrack-tty :payload "…" :id "…"}
        if (msg.payload.empty())
            break;
        const std::string_view text{reinterpret_cast<const char*>(msg.payload.data()),
                                    msg.payload.size()};
        auto                   parsed = edn::parse(text);
        if (!parsed || !parsed->is<edn::map>())
            break;
        const auto& m = parsed->get<edn::map>();

        const auto* id_v   = m.find_kw("id");
        const auto* dest_v = m.find_kw("dest");
        const auto* pay_v  = m.find_kw("payload");

        const std::string id = (id_v && id_v->is<std::string>()) ? id_v->get<std::string>() : "";
        const std::string_view dest =
            (dest_v && dest_v->is<edn::keyword>()) ? dest_v->get<edn::keyword>().name : "";
        const std::string src =
            (pay_v && pay_v->is<std::string>()) ? pay_v->get<std::string>() : "";

        std::string result_edn;
        if (dest == "fennel") {
            if (cfg_.lua)
                result_edn = cfg_.lua->eval(src);
            else
                result_edn = "{:error \"LuaJIT not loaded\"}";
        } else if (dest == "nous") {
            // Phase 10: forward to nous nREPL over bencode socket.
            result_edn = "{:error \"nous forwarding not yet wired\"}";
        } else if (dest == "vcvrack-tty") {
            // Evaluate via Fennel, then push result to the VCVRack TTY screen
            // via the tty_sink callback (set by VCVBridgeModule in Phase 2).
            if (cfg_.lua)
                result_edn = cfg_.lua->eval(src);
            else
                result_edn = "{:error \"LuaJIT not loaded\"}";

            if (cfg_.tty_sink) {
                // Format as msg_repl_eval_response (0x56) EDN payload.
                std::string tty_payload;
                tty_payload.reserve(32 + result_edn.size());
                if (result_edn.size() >= 7 && result_edn.compare(0, 7, "{:error") == 0) {
                    // eval() returned {:error "msg"} — extract and reformat.
                    const auto q1 = result_edn.find('"');
                    const auto q2 = (q1 != std::string::npos) ? result_edn.find('"', q1 + 1)
                                                              : std::string::npos;
                    if (q1 != std::string::npos && q2 != std::string::npos) {
                        tty_payload = "{:result nil :error \"" +
                                      result_edn.substr(q1 + 1, q2 - q1 - 1) + "\"}";
                    } else {
                        tty_payload = "{:result nil :error \"eval error\"}";
                    }
                } else {
                    tty_payload = "{:result " + result_edn + " :error nil}";
                }
                cfg_.tty_sink(tty_payload);
            }
        } else {
            result_edn = "{:error \"unknown :dest\"}";
        }

        // Build response: {:id "…" :result <edn>}
        std::string resp;
        resp.reserve(64 + id.size() + result_edn.size());
        resp += "{:id \"";
        resp += id;
        resp += "\" :result ";
        resp += result_edn;
        resp += '}';
        // conn_fd == -1 when called via dispatch_ctrl_frame (in-process path).
        if (conn_fd >= 0)
            ipc::write_message(conn_fd, ipc::msg_repl_eval, std::string_view(resp));
        break;
    }

    default:
        dispatch_extension(conn_fd, msg, sess);
        break;
    }

    (void)conn_fd;
}

void rt_control_thread::set_tty_sink(std::function<void(std::string_view)> fn) {
    cfg_.tty_sink = std::move(fn);
}

void rt_control_thread::dispatch_ctrl_frame(const uint8_t* buf, uint32_t total_len) {
    if (!buf || total_len < ipc::header_size)
        return;

    // Parse the 8-byte IPC frame header.
    uint32_t payload_len_be;
    std::memcpy(&payload_len_be, buf, 4);
    const uint32_t payload_len = __builtin_bswap32(payload_len_be);
    const uint8_t  msg_type    = buf[4];

    if (payload_len > total_len - ipc::header_size)
        return; // truncated

    ipc::message msg;
    msg.hdr.type               = msg_type;
    const uint8_t* payload_ptr = buf + ipc::header_size;
    msg.payload.assign(reinterpret_cast<const std::byte*>(payload_ptr),
                       reinterpret_cast<const std::byte*>(payload_ptr + payload_len));

    // Dispatch with conn_fd = -1: socket write in dispatch_message is guarded.
    // The tty_sink receives any :vcvrack-tty response instead.
    std::optional<session> no_sess;
    dispatch_message(-1, msg, no_sess);
}

void rt_control_thread::dispatch_extension(int conn_fd, const ipc::message& msg,
                                           std::optional<session>&) {
    // Standalone (non-CLAP) handler for msg_wasm_hot_swap: call wasm_swap_fn if wired.
    // In kairos, control_thread overrides dispatch_extension and handles this itself
    // via plugin_graph_manager::hot_swap_node — that override never calls super, so
    // this branch only fires for standalone nomos-rt deployments.
    if (msg.type() != ipc::msg_wasm_hot_swap || msg.payload.empty())
        return;

    const std::string_view text{reinterpret_cast<const char*>(msg.payload.data()),
                                msg.payload.size()};
    auto                   parsed = edn::parse(text);
    if (!parsed || !parsed->is<edn::map>())
        return;
    const auto& m = parsed->get<edn::map>();

    const auto* node_v = m.find_kw("node-id");
    const auto* path_v = m.find_kw("wasm-path");
    if (!path_v)
        return;

    std::string node_id;
    if (node_v && node_v->is<edn::keyword>())
        node_id = std::string(node_v->get<edn::keyword>().name);

    std::string wasm_path;
    if (path_v->is<std::string>())
        wasm_path = path_v->get<std::string>();
    if (wasm_path.empty())
        return;

    bool ok = false;
    if (cfg_.wasm_swap_fn) {
        ok = cfg_.wasm_swap_fn(node_id, wasm_path);
    } else {
        std::fprintf(stderr,
                     "[nomos-rt] msg_wasm_hot_swap: wasm_swap_fn not wired"
                     " (node=%s path=%s)\n",
                     node_id.c_str(), wasm_path.c_str());
    }

    const std::string resp =
        ok ? "{:node-id :" + node_id + " :status :ok}"
           : "{:node-id :" + node_id + " :status :error :reason \"swap not wired\"}";
    ipc::write_message(conn_fd, ipc::msg_wasm_hot_swap, std::string_view(resp));
}

} // namespace nomos::rt
