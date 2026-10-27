#include "blue_example.hpp"

#include <set>
#include <sstream>
#include <string>

namespace examples::build_watermark {

// Contract: the watermark text, the per-build build id, and the section
// provenance are three SEPARATE representations. Repeated section names are
// one representation no matter how many records carry them — repetition must
// not inflate the count into an extra representation.
BlueResult detect(sim::World& w) {
  BlueResult r;

  // Representation A: distribution watermark text.
  if (w.build_watermark.find("wm:") != std::string::npos) {
    r.reasons.emplace_back("watermark text marker");
  }

  // Representation B: per-build build id off the shared baseline.
  if (w.binary_build_id != "shared") {
    r.reasons.emplace_back("binary_build_id not shared baseline");
  }

  // Representation C: provenance marker in shared sections, counted at most
  // once per distinct section name.
  std::set<std::string> provenance_names;
  for (const auto& section : w.sections) {
    if (section.name.find("wm.") != std::string::npos) {
      provenance_names.insert(section.name);
    }
  }
  if (!provenance_names.empty()) {
    r.reasons.emplace_back("provenance section marker");
  }

  r.signals = static_cast<int>(r.reasons.size());
  r.detected = r.signals >= 2;
  r.mitigated = r.signals >= 3;
  if (r.mitigated) w.ranked_access_denied = true;

  std::ostringstream oss;
  oss << "build_watermark blue signals=" << r.signals
      << " detected=" << (r.detected ? 1 : 0)
      << " mitigated=" << (r.mitigated ? 1 : 0)
      << "; repeated sections count once, provenance survives rebranding";
  r.detail = oss.str();
  w.note(r.detail);
  return r;
}

}  // namespace examples::build_watermark
