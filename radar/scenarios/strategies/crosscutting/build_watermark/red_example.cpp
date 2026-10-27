#include "red_example.hpp"

#include <cstdio>

namespace examples::build_watermark {

// Repeated section name: the provenance marker appears in several identical
// section records, but correlation must treat the name as one representation.
constexpr const char* kProvenanceSection = "lab.wm.provenance";

RedResult apply(sim::World& w) {
  RedResult r;

  // Step 1: lab actor for the section provenance residual.
  r.actor_pid = w.spawn("build-watermark-actor.exe");
  ++r.steps;
  if (!w.proc(r.actor_pid)) {
    r.detail = "actor spawn verification failed";
    return r;
  }

  // Step 2: representation A — per-build watermark text.
  w.build_watermark = "wm:watermark-a7:distribution";
  ++r.steps;

  // Step 3: representation B — per-build build id (distinct from baseline).
  w.binary_build_id = "wm-build-a7";
  ++r.steps;

  // Step 4: representation C — shared sections carrying the provenance name.
  // The same section name is retained twice on purpose: the blue lesson is to
  // count the repeated evidence once, keeping three representations in total.
  w.add_section({kProvenanceSection, r.actor_pid, w.game_pid(), false});
  w.add_section({kProvenanceSection, r.actor_pid, w.game_pid(), false});
  ++r.steps;

  r.achieved = !w.build_watermark.empty() && w.binary_build_id != "shared" &&
               !w.sections.empty() && r.steps >= 4;
  r.detail = "build_watermark red steps=" + std::to_string(r.steps) +
             " watermark=" + w.build_watermark +
             " build_id=" + w.binary_build_id +
             " sections=" + std::to_string(w.sections.size());
  w.note(r.detail);
  return r;
}

}  // namespace examples::build_watermark
