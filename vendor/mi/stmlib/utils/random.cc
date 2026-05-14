// SPDX-License-Identifier: MIT
// Static member definition for stmlib::Random — not present in the original
// MI firmware (each firmware defines it in its own main.cc).
#include "stmlib/utils/random.h"

namespace stmlib {
uint32_t Random::rng_state_ = 0x12345678;
}  // namespace stmlib
