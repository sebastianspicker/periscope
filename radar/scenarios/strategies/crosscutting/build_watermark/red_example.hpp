#pragma once

// Lab RED example for strategy `build_watermark` on sim::World only.
// Multi-step educational scars: provenance watermark text, per-build build id,
// and shared-section provenance (repeated section names count once).

#include "sim/world.hpp"

#include <cstdint>
#include <string>

namespace examples::build_watermark {

struct RedResult {
  bool achieved = false;
  std::uint32_t actor_pid = 0;
  int steps = 0;
  std::string detail;
};

/// Multi-step red entry for `build_watermark`.
RedResult apply(sim::World& w);

}  // namespace examples::build_watermark
