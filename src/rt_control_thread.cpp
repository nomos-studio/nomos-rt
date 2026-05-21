// SPDX-License-Identifier: GPL-2.0-or-later
#include <nomos/rt/ipc.hpp>
#include <nomos/rt/rt_control_thread.hpp>

#include <nomos/rt/modulator_engine.hpp>
#include <nomos/rt/modulator_registry.hpp>
#include "graph_modulator.hpp"
#include "slope_modulator.hpp"
#include "segment_modulator.hpp"
#include "slew_modulator.hpp"
#include "shift_register_modulator.hpp"
#include "fractal_modulator.hpp"
#include "stochastic_modulator.hpp"
#include "cv_channel_decoder.hpp"
#include "divine_cmos_modulator.hpp"
#include "statues_modulator.hpp"
#include "cipher_modulator.hpp"
#include "bools_ring_modulator.hpp"

#include <edn/builtins.hpp>
#include <edn/parser.hpp>
#include <txlog/txlog.hpp>

#include <clap/events.h>

#include <cerrno>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>

namespace {

// Parse the canonical UUID string "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" into bytes.
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
            ::listen(fd, 1) < 0) {
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

    running_.store(true, std::memory_order_release);
    thread_ = std::thread(&rt_control_thread::run, this);
}

void rt_control_thread::stop() {
    if (!running_.exchange(false, std::memory_order_acq_rel))
        return;

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
        if (conn_fd < 0)
            break;

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
        // Bundle of beat-accurate events: {:at-beat D :events [{:at-tick N :type :kw ...}]}
        // Each event's beat = at-beat + at-tick / 24.0.
        // Requires sched_staging to be wired; silently drops the bundle otherwise.
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
        // {:id :kw :type :slope :rate 1.0 :shape 0 :slope 0 :smoothness 0 :depth 1 :bipolar 1}
        if (!cfg_.mod_engine || msg.payload.empty())
            break;
        const std::string_view text{reinterpret_cast<const char*>(msg.payload.data()),
                                    msg.payload.size()};
        auto parsed = edn::parse(text);
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
            if (!v) return def;
            if (v->is<double>())   return static_cast<float>(v->get<double>());
            if (v->is<int64_t>())  return static_cast<float>(v->get<int64_t>());
            return def;
        };

        if (type_name == "slope") {
            auto mod = std::make_unique<slope_modulator>();
            mod->update("rate",       get_f("rate",       1.0f));
            mod->update("shape",      get_f("shape",      0.0f));
            mod->update("slope",      get_f("slope",      0.0f));
            mod->update("smoothness", get_f("smoothness", 0.0f));
            mod->update("depth",      get_f("depth",      1.0f));
            mod->update("bipolar",    get_f("bipolar",    1.0f));
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
                {"alt",  segment_modulator::type::alt},
            };

            std::vector<segment_modulator::segment_def> defs;
            for (const auto& item : segs_v->get<edn::vector>().items) {
                if (!item.is<edn::map>()) continue;
                const auto& sm = item.get<edn::map>();

                segment_modulator::segment_def def;

                if (const auto* t = sm.find_kw("type"); t && t->is<edn::keyword>()) {
                    auto it = type_map.find(std::string{t->get<edn::keyword>().name});
                    if (it != type_map.end()) def.kind = it->second;
                }

                auto get_sf = [&](const char* kw, float d) -> float {
                    const auto* v = sm.find_kw(kw);
                    if (!v) return d;
                    if (v->is<double>())  return static_cast<float>(v->get<double>());
                    if (v->is<int64_t>()) return static_cast<float>(v->get<int64_t>());
                    return d;
                };

                def.primary   = get_sf("primary",   0.5f);
                def.secondary = get_sf("secondary",  0.5f);

                if (const auto* l = sm.find_kw("loop"); l)
                    def.loop = !(l->is_nil() || (l->is<bool>() && !l->get<bool>()));

                defs.push_back(def);
            }

            if (!defs.empty())
                cfg_.mod_engine->start(std::move(id),
                    std::make_unique<segment_modulator>(std::span{defs}));

        } else if (type_name == "slew") {
            auto mod = std::make_unique<slew_modulator>();
            mod->update("rise",  get_f("rise",  0.1f));
            mod->update("fall",  get_f("fall",  0.1f));
            mod->update("cycle", get_f("cycle", 0.0f));
            mod->update("depth", get_f("depth", 1.0f));
            cfg_.mod_engine->start(std::move(id), std::move(mod));

        } else if (type_name == "shift-register") {
            // :mode keyword selects feedback algorithm; length and dac_bits are
            // constructor-fixed so they can't be updated live.
            static const std::unordered_map<std::string, shift_register_modulator::mode> mode_map{
                {"lfsr",    shift_register_modulator::mode::lfsr},
                {"rungler", shift_register_modulator::mode::rungler},
                {"turing",  shift_register_modulator::mode::turing},
                {"open",    shift_register_modulator::mode::open},
            };
            shift_register_modulator::mode sr_mode = shift_register_modulator::mode::turing;
            if (const auto* mv = m.find_kw("mode"); mv && mv->is<edn::keyword>()) {
                auto it = mode_map.find(std::string{mv->get<edn::keyword>().name});
                if (it != mode_map.end()) sr_mode = it->second;
            }
            const int length   = static_cast<int>(get_f("length",   16.0f));
            const int dac_bits = static_cast<int>(get_f("dac_bits",  3.0f));
            auto mod = std::make_unique<shift_register_modulator>(sr_mode, length, dac_bits);
            mod->update("clock_rate", get_f("clock_rate", 2.0f));
            mod->update("data",       get_f("data",       0.5f));
            mod->update("param",      get_f("param",      0.5f));
            mod->update("depth",      get_f("depth",      1.0f));
            cfg_.mod_engine->start(std::move(id), std::move(mod));

        } else if (type_name == "fractal") {
            auto mod = std::make_unique<fractal_modulator>();
            mod->update("base_rate",   get_f("base_rate",   0.1f));
            mod->update("octaves",     get_f("octaves",     4.0f));
            mod->update("lacunarity",  get_f("lacunarity",  2.0f));
            mod->update("persistence", get_f("persistence", 0.5f));
            mod->update("depth",       get_f("depth",       1.0f));
            cfg_.mod_engine->start(std::move(id), std::move(mod));

        } else if (type_name == "stochastic") {
            auto mod = std::make_unique<stochastic_modulator>();
            mod->update("rate",    get_f("rate",    2.0f));
            mod->update("bias",    get_f("bias",    0.5f));
            mod->update("spread",  get_f("spread",  0.5f));
            mod->update("deja_vu", get_f("deja_vu", 0.0f));
            mod->update("length",  get_f("length",  8.0f));
            mod->update("depth",   get_f("depth",   1.0f));
            cfg_.mod_engine->start(std::move(id), std::move(mod));

        } else if (type_name == "graph") {
            const auto* graph_v = m.find_kw("graph");
            if (!graph_v) break;

            std::unordered_map<std::string, float> params;
            if (const auto* pv = m.find_kw("params"); pv && pv->is<edn::map>()) {
                for (const auto& [k, v] : pv->get<edn::map>().entries) {
                    if (!k.is<edn::keyword>()) continue;
                    float fv = 0.0f;
                    if (v.is<double>())        fv = static_cast<float>(v.get<double>());
                    else if (v.is<int64_t>())  fv = static_cast<float>(v.get<int64_t>());
                    else continue;
                    params[std::string(k.get<edn::keyword>().name)] = fv;
                }
            }

            cfg_.mod_engine->start(std::move(id),
                std::make_unique<graph_modulator>(
                    parse_control_graph(*graph_v, std::move(params)),
                    cfg_.mod_engine));

        } else if (type_name == "cv-channel-decoder") {
            // {:id :kw :type :cv-channel-decoder
            //  :span [:source-id]          ; or [:source-id :aux] / [:source-id :gate]
            //  :channels 1                 ; 1..8
            //  :space 1.0}
            int channels = 1;
            if (const auto* v = m.find_kw("channels")) {
                if (v->is<int64_t>())  channels = static_cast<int>(v->get<int64_t>());
                else if (v->is<double>()) channels = static_cast<int>(v->get<double>());
            }

            std::string              source_id;
            cv_channel_decoder::source_field field = cv_channel_decoder::source_field::cv;

            if (const auto* sv = m.find_kw("span")) {
                if (sv->is<edn::vector>()) {
                    const auto& vec = sv->get<edn::vector>().items;
                    if (!vec.empty() && vec[0].is<edn::keyword>())
                        source_id = std::string{vec[0].get<edn::keyword>().name};
                    if (vec.size() >= 2 && vec[1].is<edn::keyword>()) {
                        const auto fn = vec[1].get<edn::keyword>().name;
                        if (fn == "aux")  field = cv_channel_decoder::source_field::aux;
                        if (fn == "gate") field = cv_channel_decoder::source_field::gate;
                    }
                }
            }

            auto mod = std::make_unique<cv_channel_decoder>(
                channels, cfg_.mod_engine, std::move(source_id), field);
            mod->update("space",   get_f("space",   1.0f));
            mod->update("clocked", get_f("clocked", 0.0f));

            // Static span value (used when no source_id / as initial value).
            if (const auto* sv = m.find_kw("span"); sv && !sv->is<edn::vector>()) {
                if (sv->is<double>())  mod->update("span", static_cast<float>(sv->get<double>()));
                else if (sv->is<int64_t>()) mod->update("span", static_cast<float>(sv->get<int64_t>()));
            }

            cfg_.mod_engine->start(std::move(id), std::move(mod));

        } else if (type_name == "divine-cmos") {
            // {:id :kw :type :divine-cmos
            //  :clock1 [:src-id]  :clock2 [:src-id]
            //  :gains [g0 g1 g2 g3]  :slew 0.0}
            auto get_src = [&](const char* kw) -> std::string {
                const auto* v = m.find_kw(kw);
                if (!v || !v->is<edn::vector>()) return {};
                const auto& vec = v->get<edn::vector>().items;
                if (!vec.empty() && vec[0].is<edn::keyword>())
                    return std::string{vec[0].get<edn::keyword>().name};
                return {};
            };
            auto mod = std::make_unique<divine_cmos_modulator>(
                cfg_.mod_engine, get_src("clock1"), get_src("clock2"));
            if (const auto* gv = m.find_kw("gains"); gv && gv->is<edn::vector>()) {
                const auto& items = gv->get<edn::vector>().items;
                for (int i = 0; i < 4 && i < static_cast<int>(items.size()); ++i) {
                    float g = 1.0f;
                    if (items[i].is<double>())  g = static_cast<float>(items[i].get<double>());
                    else if (items[i].is<int64_t>()) g = static_cast<float>(items[i].get<int64_t>());
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
                if (!v || !v->is<edn::vector>()) return {};
                const auto& vec = v->get<edn::vector>().items;
                if (!vec.empty() && vec[0].is<edn::keyword>())
                    return std::string{vec[0].get<edn::keyword>().name};
                return {};
            };
            auto mod = std::make_unique<statues_modulator>(
                cfg_.mod_engine, get_src("in"), get_src("addr"));
            cfg_.mod_engine->start(std::move(id), std::move(mod));

        } else if (type_name == "cipher") {
            // {:id :kw :type :cipher
            //  :clock [:src] :data1 [:src] :data2 [:src] :strobe [:src]}
            auto get_src = [&](const char* kw) -> std::string {
                const auto* v = m.find_kw(kw);
                if (!v || !v->is<edn::vector>()) return {};
                const auto& vec = v->get<edn::vector>().items;
                if (!vec.empty() && vec[0].is<edn::keyword>())
                    return std::string{vec[0].get<edn::keyword>().name};
                return {};
            };
            auto mod = std::make_unique<cipher_modulator>(
                cfg_.mod_engine,
                get_src("clock"), get_src("data1"), get_src("data2"), get_src("strobe"));
            cfg_.mod_engine->start(std::move(id), std::move(mod));

        } else if (type_name == "bools-ring") {
            // {:id :kw :type :bools-ring
            //  :mode :xor  :slew 0.0
            //  :ins {:in0 [:src] :in1 [:src] :in2 [:src] :in3 [:src]}}
            static const std::unordered_map<std::string, bools_ring_modulator::mode> mode_map{
                {"xor",  bools_ring_modulator::mode::xor_mode},
                {"or",   bools_ring_modulator::mode::or_mode},
                {"and",  bools_ring_modulator::mode::and_mode},
                {"nor",  bools_ring_modulator::mode::nor_mode},
                {"nand", bools_ring_modulator::mode::nand_mode},
                {"xnor", bools_ring_modulator::mode::xnor_mode},
            };
            auto br_mode = bools_ring_modulator::mode::xor_mode;
            if (const auto* mv = m.find_kw("mode"); mv && mv->is<edn::keyword>()) {
                auto it = mode_map.find(std::string{mv->get<edn::keyword>().name});
                if (it != mode_map.end()) br_mode = it->second;
            }

            // Parse :ins sub-map for per-input source IDs.
            auto get_in_src = [&](const char* kw) -> std::string {
                const auto* ins_v = m.find_kw("ins");
                if (!ins_v || !ins_v->is<edn::map>()) return {};
                const auto* v = ins_v->get<edn::map>().find_kw(kw);
                if (!v || !v->is<edn::vector>()) return {};
                const auto& vec = v->get<edn::vector>().items;
                if (!vec.empty() && vec[0].is<edn::keyword>())
                    return std::string{vec[0].get<edn::keyword>().name};
                return {};
            };

            auto mod = std::make_unique<bools_ring_modulator>(
                br_mode, cfg_.mod_engine,
                get_in_src("in0"), get_in_src("in1"),
                get_in_src("in2"), get_in_src("in3"), /*sample_src=*/"");
            mod->update("slew", get_f("slew", 0.0f));
            cfg_.mod_engine->start(std::move(id), std::move(mod));

        } else if (cfg_.ext_registry) {
            // Extension type: factory constructs, then float params are applied generically.
            auto mod = cfg_.ext_registry->make(type_name);
            if (mod) {
                for (const auto& [k, v] : m.entries) {
                    if (!k.is<edn::keyword>()) continue;
                    const std::string_view key = k.get<edn::keyword>().name;
                    if (key == "id" || key == "type") continue;
                    float fv = 0.0f;
                    if (v.is<double>())        fv = static_cast<float>(v.get<double>());
                    else if (v.is<int64_t>())  fv = static_cast<float>(v.get<int64_t>());
                    else continue;
                    mod->update(key, fv);
                }
                cfg_.mod_engine->start(std::move(id), std::move(mod));
            }
        }
        break;
    }

    case ipc::msg_modulator_stop: {
        // {:id :kw}
        if (!cfg_.mod_engine || msg.payload.empty())
            break;
        const std::string_view text{reinterpret_cast<const char*>(msg.payload.data()),
                                    msg.payload.size()};
        auto parsed = edn::parse(text);
        if (!parsed || !parsed->is<edn::map>())
            break;
        const auto& m    = parsed->get<edn::map>();
        const auto* id_v = m.find_kw("id");
        if (!id_v) break;

        std::string id;
        if (id_v->is<edn::keyword>())       id = id_v->get<edn::keyword>().name;
        else if (id_v->is<std::string>())   id = id_v->get<std::string>();
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
        auto parsed = edn::parse(text);
        if (!parsed || !parsed->is<edn::map>())
            break;
        const auto& m = parsed->get<edn::map>();

        const auto* id_v    = m.find_kw("id");
        const auto* key_v   = m.find_kw("key");
        const auto* value_v = m.find_kw("value");
        if (!id_v || !key_v || !value_v)
            break;

        std::string id;
        if (id_v->is<edn::keyword>())      id = id_v->get<edn::keyword>().name;
        else if (id_v->is<std::string>())  id = id_v->get<std::string>();
        if (id.empty()) break;

        std::string key;
        if (key_v->is<std::string>())      key = key_v->get<std::string>();
        else if (key_v->is<edn::keyword>()) key = key_v->get<edn::keyword>().name;
        if (key.empty()) break;

        float value = 0.0f;
        if (value_v->is<double>())         value = static_cast<float>(value_v->get<double>());
        else if (value_v->is<int64_t>())   value = static_cast<float>(value_v->get<int64_t>());

        cfg_.mod_engine->update_param(id, key, value);
        break;
    }

    default:
        dispatch_extension(conn_fd, msg, sess);
        break;
    }

    (void)conn_fd;
}

void rt_control_thread::dispatch_extension(int, const ipc::message&, std::optional<session>&) {
    // no-op — derived classes override to handle additional message types
}

} // namespace nomos::rt
