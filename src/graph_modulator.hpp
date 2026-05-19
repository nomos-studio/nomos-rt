// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <nomos/rt/abstract_modulator.hpp>

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace edn { class value; }
namespace nomos::rt { class modulator_engine; }

namespace nomos::rt {

// ---------------------------------------------------------------------------
// cg_node — one node in a control graph
//
// Nodes are stored in topological order (children before parents) because the
// recursive parser appends each node only after all its inputs have been
// appended.  Evaluation is therefore a simple forward pass with no sorting step.
//
// State slots:
//   phasor:      state[0] = accumulated phase  [0, 1)
//   slew:        state[0] = current output value
//   sample_hold: state[0] = held value, state[1] = previous gate value
//   beat_phase:  stateless — phase = fmod(beat / period_beats, 1.0)
// ---------------------------------------------------------------------------
struct cg_node {
    enum class kind {
        const_val,
        param_ref,   // named param from control_graph::params
        mod_ref,     // output of another named modulator in the engine
        beat_in,     // current beat position as a float
        beat_phase,  // fmod(beat / period_beats, 1.0) — musically-locked phase
        phasor,
        sin, cos, tri, saw, square,
        scale, clamp, add, mul, neg, abs_val, mix,
        slew,
        sample_hold,
        threshold,
        comparator,
    };

    kind                 type{kind::const_val};
    std::array<int, 4>   inputs{-1, -1, -1, -1}; // node indices; -1 = unused
    std::array<float, 4> args{};

    // param_ref: parameter name
    // mod_ref:   source modulator id; args[0] encodes output field
    //            (0=cv, 1=aux, 2=gate, 3=gate2)
    std::string param_key;

    float state[2]{};
};

// ---------------------------------------------------------------------------
// control_graph — the compiled form of an EDN s-expression graph
// ---------------------------------------------------------------------------
struct control_graph {
    std::vector<cg_node>                   nodes;
    std::unordered_map<std::string, float> params;    // live-updatable named values
    int out_cv{-1}, out_aux{-1}, out_gate{-1}, out_gate2{-1};
};

// Parse an EDN graph expression into a control_graph.  graph_expr may be:
//   - A vector s-expression:  [:sin [:phasor [:param :rate]]]  → cv output only
//   - A map of outputs:       {:cv [...] :gate [...]}          → multi-output
// params is pre-populated from the :params map in the IPC message.
// Returns a graph with no nodes on parse error (produces silent output).
control_graph parse_control_graph(
    const edn::value&                      graph_expr,
    std::unordered_map<std::string, float> params = {});

// ---------------------------------------------------------------------------
// graph_modulator — abstract_modulator backed by an interpreted control graph
// ---------------------------------------------------------------------------
class graph_modulator final : public abstract_modulator {
public:
    // engine may be nullptr (cross-modulator mod_ref nodes will return 0).
    explicit graph_modulator(control_graph graph,
                             const modulator_engine* engine = nullptr);

    modulator_output tick(double beat, float tick_rate_hz) override;
    void             update(std::string_view key, float value) override;

private:
    control_graph           graph_;
    const modulator_engine* engine_;
    std::vector<float>      values_; // per-node scratch buffer, reused each tick
};

} // namespace nomos::rt
