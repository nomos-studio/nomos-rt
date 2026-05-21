// SPDX-License-Identifier: LGPL-2.1-or-later
#include "graph_modulator.hpp"

#include <nomos/rt/modulator_engine.hpp>

#include <edn/value.hpp>

#include <algorithm>
#include <cmath>

namespace nomos::rt {

namespace {

static constexpr float kTwoPi = 6.283185307f;

// Parse one EDN value into a node, appending it (and any child nodes) to g.
// Returns the node index, or -1 on error.
int parse_node(const edn::value& v, control_graph& g) {
    if (v.is<double>()) {
        g.nodes.push_back({.type = cg_node::kind::const_val,
                           .args = {static_cast<float>(v.get<double>())}});
        return static_cast<int>(g.nodes.size()) - 1;
    }
    if (v.is<int64_t>()) {
        g.nodes.push_back({.type = cg_node::kind::const_val,
                           .args = {static_cast<float>(v.get<int64_t>())}});
        return static_cast<int>(g.nodes.size()) - 1;
    }
    if (!v.is<edn::vector>()) return -1;

    const auto& items = v.get<edn::vector>().items;
    if (items.empty() || !items[0].is<edn::keyword>()) return -1;

    const std::string_view op = items[0].get<edn::keyword>().name;

    // child(i): parse items[i+1] as a sub-expression, return its node index.
    // child_or_const(i, def): same, but create a const node for def if missing.
    auto child = [&](int i) -> int {
        if (static_cast<int>(items.size()) > i + 1)
            return parse_node(items[i + 1], g);
        return -1;
    };
    auto child_or_const = [&](int i, float def) -> int {
        if (static_cast<int>(items.size()) > i + 1)
            return parse_node(items[i + 1], g);
        g.nodes.push_back({.type = cg_node::kind::const_val, .args = {def}});
        return static_cast<int>(g.nodes.size()) - 1;
    };

    cg_node node{};

    if (op == "const") {
        node.type     = cg_node::kind::const_val;
        node.args[0]  = (items.size() > 1 && items[1].is<double>())
                        ? static_cast<float>(items[1].get<double>()) : 0.0f;

    } else if (op == "param") {
        node.type = cg_node::kind::param_ref;
        if (items.size() > 1 && items[1].is<edn::keyword>())
            node.param_key = std::string(items[1].get<edn::keyword>().name);

    } else if (op == "mod-out") {
        node.type = cg_node::kind::mod_ref;
        if (items.size() > 1 && items[1].is<edn::keyword>())
            node.param_key = std::string(items[1].get<edn::keyword>().name);
        if (items.size() > 2 && items[2].is<edn::keyword>()) {
            const auto field = items[2].get<edn::keyword>().name;
            node.args[0] = (field == "aux")   ? 1.0f
                         : (field == "gate")  ? 2.0f
                         : (field == "gate2") ? 3.0f
                         :                     0.0f;
        }

    } else if (op == "beat") {
        node.type = cg_node::kind::beat_in;

    } else if (op == "beat-phase") {
        // [:beat-phase period-beats] — phase = fmod(beat / period, 1.0)
        // period-beats may be a literal or any sub-expression.
        node.type      = cg_node::kind::beat_phase;
        node.inputs[0] = child_or_const(0, 1.0f);

    } else if (op == "phasor") {
        node.type      = cg_node::kind::phasor;
        node.inputs[0] = child_or_const(0, 1.0f);  // rate

    } else if (op == "sin") {
        node.type      = cg_node::kind::sin;
        node.inputs[0] = child(0);

    } else if (op == "cos") {
        node.type      = cg_node::kind::cos;
        node.inputs[0] = child(0);

    } else if (op == "tri") {
        node.type      = cg_node::kind::tri;
        node.inputs[0] = child(0);

    } else if (op == "saw") {
        node.type      = cg_node::kind::saw;
        node.inputs[0] = child(0);

    } else if (op == "square") {
        node.type      = cg_node::kind::square;
        node.inputs[0] = child(0);                   // phase
        node.inputs[1] = child_or_const(1, 0.5f);   // width

    } else if (op == "scale") {
        node.type      = cg_node::kind::scale;
        node.inputs[0] = child(0);                   // value
        node.inputs[1] = child_or_const(1, 0.0f);   // min
        node.inputs[2] = child_or_const(2, 1.0f);   // max

    } else if (op == "clamp") {
        node.type      = cg_node::kind::clamp;
        node.inputs[0] = child(0);
        node.inputs[1] = child_or_const(1, 0.0f);
        node.inputs[2] = child_or_const(2, 1.0f);

    } else if (op == "add") {
        node.type      = cg_node::kind::add;
        node.inputs[0] = child(0);
        node.inputs[1] = child(1);

    } else if (op == "mul") {
        node.type      = cg_node::kind::mul;
        node.inputs[0] = child(0);
        node.inputs[1] = child(1);

    } else if (op == "neg") {
        node.type      = cg_node::kind::neg;
        node.inputs[0] = child(0);

    } else if (op == "abs") {
        node.type      = cg_node::kind::abs_val;
        node.inputs[0] = child(0);

    } else if (op == "mix") {
        // [:mix a b t] — lerp: a*(1-t) + b*t
        node.type      = cg_node::kind::mix;
        node.inputs[0] = child(0);                   // a
        node.inputs[1] = child(1);                   // b
        node.inputs[2] = child_or_const(2, 0.5f);   // t

    } else if (op == "slew") {
        // [:slew signal rise-seconds fall-seconds]
        node.type      = cg_node::kind::slew;
        node.inputs[0] = child(0);                    // signal
        node.inputs[1] = child_or_const(1, 0.01f);   // rise
        node.inputs[2] = child_or_const(2, 0.01f);   // fall (defaults to rise if omitted)

    } else if (op == "sample-hold") {
        // [:sample-hold signal gate]
        node.type      = cg_node::kind::sample_hold;
        node.inputs[0] = child(0);  // signal
        node.inputs[1] = child(1);  // gate (samples on rising edge >0.5)

    } else if (op == "threshold") {
        // [:threshold value level] — gate output: 1 if value > level
        node.type      = cg_node::kind::threshold;
        node.inputs[0] = child(0);
        node.inputs[1] = child_or_const(1, 0.5f);

    } else if (op == "comparator") {
        // [:comparator a b] — gate output: 1 if a > b
        node.type      = cg_node::kind::comparator;
        node.inputs[0] = child(0);
        node.inputs[1] = child(1);

    } else {
        return -1;  // unknown operator — propagate error upward
    }

    g.nodes.push_back(std::move(node));
    return static_cast<int>(g.nodes.size()) - 1;
}

// ---------------------------------------------------------------------------

float eval_node(cg_node&                                       n,
                const std::vector<float>&                      vals,
                const control_graph&                           g,
                const modulator_engine*                        engine,
                double                                         beat,
                float                                          dt) {
    auto in = [&](int slot) -> float {
        int idx = n.inputs[slot];
        return (idx >= 0 && idx < static_cast<int>(vals.size())) ? vals[idx] : 0.0f;
    };

    switch (n.type) {

    case cg_node::kind::const_val:
        return n.args[0];

    case cg_node::kind::param_ref: {
        auto it = g.params.find(n.param_key);
        return it != g.params.end() ? it->second : 0.0f;
    }

    case cg_node::kind::mod_ref: {
        if (!engine) return 0.0f;
        const auto* out = engine->last_output(n.param_key);
        if (!out) return 0.0f;
        const int field = static_cast<int>(n.args[0]);
        switch (field) {
            case 1:  return out->aux;
            case 2:  return out->gate  ? 1.0f : 0.0f;
            case 3:  return out->gate2 ? 1.0f : 0.0f;
            default:
                // fields 4..11 → outputs[0..7]
                if (field >= 4 && field < 4 + modulator_output::kMaxOutputs)
                    return out->outputs[field - 4];
                return out->cv;
        }
    }

    case cg_node::kind::beat_in:
        return static_cast<float>(beat);

    case cg_node::kind::beat_phase: {
        float period = in(0);
        if (period <= 0.0f) return 0.0f;
        return std::fmod(static_cast<float>(beat) / period, 1.0f);
    }

    case cg_node::kind::phasor: {
        float  rate = in(0);
        float& ph   = n.state[0];
        float  out  = ph;           // return phase at start of this tick
        ph += rate * dt;
        if (ph >= 1.0f) ph -= std::floor(ph);
        return out;
    }

    case cg_node::kind::sin:   return std::sin(in(0) * kTwoPi);
    case cg_node::kind::cos:   return std::cos(in(0) * kTwoPi);

    case cg_node::kind::tri: {
        float p = in(0);
        // bipolar triangle [-1, 1]: -1 at p=0, +1 at p=0.5, -1 at p=1
        return p < 0.5f ? 4.0f * p - 1.0f : 3.0f - 4.0f * p;
    }

    case cg_node::kind::saw:
        return in(0) * 2.0f - 1.0f;  // [0,1] → [-1,1] upward sawtooth

    case cg_node::kind::square:
        return in(0) < in(1) ? 1.0f : -1.0f;

    case cg_node::kind::scale: {
        float v = in(0), lo = in(1), hi = in(2);
        return lo + v * (hi - lo);
    }

    case cg_node::kind::clamp:
        return std::clamp(in(0), in(1), in(2));

    case cg_node::kind::add: return in(0) + in(1);
    case cg_node::kind::mul: return in(0) * in(1);
    case cg_node::kind::neg: return -in(0);
    case cg_node::kind::abs_val: return std::abs(in(0));

    case cg_node::kind::mix: {
        float t = std::clamp(in(2), 0.0f, 1.0f);
        return in(0) * (1.0f - t) + in(1) * t;
    }

    case cg_node::kind::slew: {
        float  target = in(0);
        float  rise   = in(1);
        float  fall   = in(2);
        float& cur    = n.state[0];
        if (target > cur)
            cur = (rise > 0.0f) ? std::min(cur + dt / rise,  target) : target;
        else
            cur = (fall > 0.0f) ? std::max(cur - dt / fall, target) : target;
        return cur;
    }

    case cg_node::kind::sample_hold: {
        float  sig       = in(0);
        float  gate      = in(1);
        float& held      = n.state[0];
        float& prev_gate = n.state[1];
        if (gate > 0.5f && prev_gate <= 0.5f)   // rising edge
            held = sig;
        prev_gate = gate;
        return held;
    }

    case cg_node::kind::threshold:
        return in(0) > in(1) ? 1.0f : 0.0f;

    case cg_node::kind::comparator:
        return in(0) > in(1) ? 1.0f : 0.0f;
    }
    return 0.0f;
}

} // namespace

// ---------------------------------------------------------------------------

control_graph parse_control_graph(const edn::value&                      graph_expr,
                                  std::unordered_map<std::string, float> params) {
    control_graph g;
    g.params = std::move(params);

    if (graph_expr.is<edn::map>()) {
        // Multi-output form: {:cv expr :gate expr ...}
        const auto& m = graph_expr.get<edn::map>();
        auto parse_out = [&](const char* kw) -> int {
            const auto* v = m.find_kw(kw);
            return v ? parse_node(*v, g) : -1;
        };
        g.out_cv    = parse_out("cv");
        g.out_aux   = parse_out("aux");
        g.out_gate  = parse_out("gate");
        g.out_gate2 = parse_out("gate2");
    } else {
        // Single expression → cv output
        g.out_cv = parse_node(graph_expr, g);
    }

    return g;
}

// ---------------------------------------------------------------------------

graph_modulator::graph_modulator(control_graph graph, const modulator_engine* engine)
    : graph_(std::move(graph))
    , engine_(engine)
    , values_(graph_.nodes.size(), 0.0f) {}

modulator_output graph_modulator::tick(double beat, float tick_rate_hz) {
    const float dt = tick_rate_hz > 0.0f ? 1.0f / tick_rate_hz : 0.0f;

    values_.resize(graph_.nodes.size(), 0.0f);

    for (int i = 0; i < static_cast<int>(graph_.nodes.size()); ++i)
        values_[i] = eval_node(graph_.nodes[i], values_, graph_, engine_, beat, dt);

    modulator_output out;
    if (graph_.out_cv    >= 0) out.cv    = values_[graph_.out_cv];
    if (graph_.out_aux   >= 0) out.aux   = values_[graph_.out_aux];
    if (graph_.out_gate  >= 0) out.gate  = values_[graph_.out_gate]  > 0.5f;
    if (graph_.out_gate2 >= 0) out.gate2 = values_[graph_.out_gate2] > 0.5f;
    return out;
}

void graph_modulator::update(std::string_view key, float value) {
    graph_.params[std::string{key}] = value;
}

} // namespace nomos::rt
