#pragma once

// Lab BLUE example for strategy `build_watermark` on sim::World only.
// Multi-reason educational detect/mitigate on World scars.

#include "sim/world.hpp"

#include <string>
#include <vector>

namespace examples::build_watermark {

struct BlueResult {
  bool detected = false;
  bool mitigated = false;
  int signals = 0;
  std::vector<std::string> reasons;
  std::string detail;
};

/// Multi-reason blue entry for `build_watermark`.
BlueResult detect(sim::World& w);

}  // namespace examples::build_watermark
