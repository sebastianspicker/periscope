#include "strategies/pair_util.hpp"
#include "blue_example.hpp"
#include "red_example.hpp"

namespace strategies {
namespace {
StrategyResult run(sim::World& w, sim::Narrator& n) {
  const auto red = examples::build_watermark::apply(w);
  const auto blue = examples::build_watermark::detect(w);
  StrategyResult r{red.achieved, blue.detected, blue.mitigated,
                   red.detail + " | " + blue.detail};
  n.result(r.blue_detected || r.blue_mitigated, r.summary);
  return r;
}
}  // namespace
StrategyEntry entry_109_build_watermark() {
  return {{"109_build_watermark", "build watermark", Family::Delivery, "all",
           "Plant durable build-provenance marks across watermark, build id, and section name",
           "Correlate provenance representations with repeated sections counted once"}, run};
}
}  // namespace strategies
