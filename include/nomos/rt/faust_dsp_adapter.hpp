// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

// faust_dsp_adapter<Dsp>
//
// Bridges a Faust-generated mydsp class to nomos::rt::dsp_block so that
// it can be owned by faust_modulator.
//
// This header requires a Faust installation (faust/dsp/dsp.h and
// faust/gui/MapUI.h).  It is included only by alembic-generated modulator
// wrappers and the unit tests that exercise the full pipeline.
//
// Usage:
//   #include "mydsp_generated.hpp"   // the Faust-emitted class
//   #include "nomos/rt/faust_dsp_adapter.hpp"
//   #include "faust_modulator.hpp"
//
//   auto mod = std::make_unique<nomos::rt::faust_modulator>(
//       std::make_unique<nomos::rt::faust_dsp_adapter<mydsp>>());

#include "dsp_block.hpp"

#include <faust/dsp/dsp.h>
#include <faust/gui/MapUI.h>

#include <memory>
#include <string>

namespace nomos::rt {

template <typename FaustDsp>
class faust_dsp_adapter final : public dsp_block {
public:
    faust_dsp_adapter() {
        impl_.buildUserInterface(&ui_);
    }

    void init(float sample_rate) override {
        impl_.init(static_cast<int>(sample_rate));
    }

    int num_outputs() const override {
        return const_cast<FaustDsp&>(impl_).getNumOutputs();
    }

    void set_param(std::string_view key, float value) override {
        ui_.setParamValue(std::string(key), value);
    }

    void process(float** outputs) override {
        impl_.compute(1, nullptr, outputs);
    }

private:
    FaustDsp impl_;
    MapUI    ui_;
};

} // namespace nomos::rt
