#include "cs2_radar.hpp"
#include "cs2/radar_console.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace t4_cs2 {
namespace { void mark(bool& b, bool hit, const char* why, BlueSensorReport& r) { b = hit; if (hit) r.reasons.emplace_back(why); } void finish(BlueSensorReport& r) { r.signal_count = static_cast<int>(r.reasons.size()); r.risk = std::min(r.signal_count * .25, 1.0); } }
std::vector<RedCapability> list_red_capabilities() {
  static const std::vector<RedCapability> caps{{"dma_device", "Present an external DMA device", true}, {"iommu_off", "Disable DMA remapping", true}, {"capture_card", "Present a capture card", true}, {"desktop_dup", "Use desktop duplication", true}, {"external_clone", "Clone the external display", true}, {"lag_switch", "Activate lag switch bursts", true}, {"dual_boot", "Use alternate boot profile", true}, {"capture_cv_hid", "Combine capture CV with HID input", true}, {"clipcursor", "Confine cursor for off-box aim", true}, {"packet_loss_fake", "Fake packet loss", true}};
  return caps;
}
void Cs2Radar::apply_technique(const char* name) {
  if (!name) return; const auto activate = [](bool& f, int& n) { if (!f) { f = true; ++n; } };
  if (!std::strcmp(name, "dma_device")) { activate(techniques_.dma_device, techniques_.active_count); world_.trust.dma_device_present = true; }
  else if (!std::strcmp(name, "iommu_off")) { activate(techniques_.iommu_off, techniques_.active_count); world_.trust.iommu_on = false; }
  else if (!std::strcmp(name, "capture_card")) { activate(techniques_.capture_card, techniques_.active_count); world_.trust.capture_card_present = true; }
  else if (!std::strcmp(name, "desktop_dup")) { activate(techniques_.desktop_dup, techniques_.active_count); world_.desktop_duplication = true; }
  else if (!std::strcmp(name, "external_clone")) { activate(techniques_.external_clone, techniques_.active_count); world_.external_display_clone = true; }
  else if (!std::strcmp(name, "lag_switch")) { activate(techniques_.lag_switch, techniques_.active_count); world_.lag_switch_active = true; world_.lag_switch_drop_bursts = 3; }
  else if (!std::strcmp(name, "dual_boot")) { activate(techniques_.dual_boot, techniques_.active_count); world_.trust.dual_boot_profile = true; }
  else if (!std::strcmp(name, "capture_cv_hid")) { activate(techniques_.capture_cv_hid, techniques_.active_count); world_.trust.capture_card_present = true; world_.push_input({0.0, "raw_hid", 4.f, -2.f}); }
  else if (!std::strcmp(name, "clipcursor")) { activate(techniques_.clipcursor, techniques_.active_count); world_.clipcursor_confined = true; }
  else if (!std::strcmp(name, "packet_loss_fake")) { activate(techniques_.packet_loss_fake, techniques_.active_count); world_.packet_loss_faked = true; }
  else return; std::printf("[red:T4] applied %s\n", name);
}
void Cs2Radar::apply_all_techniques() { for (const auto& c : list_red_capabilities()) apply_technique(c.name); }
void Cs2Radar::clear_techniques() { world_.trust.dma_device_present = world_.trust.capture_card_present = world_.desktop_duplication = world_.external_display_clone = world_.lag_switch_active = world_.trust.dual_boot_profile = world_.clipcursor_confined = world_.packet_loss_faked = false; world_.trust.iommu_on = true; world_.lag_switch_drop_bursts = 0; world_.inputs.clear(); techniques_ = {}; }
TechniqueProfile Cs2Radar::get_technique_profile(const char* name) {
  if (!std::strcmp(name, "dma_device")) return {name, 0.15, 2, "external DMA device present"};
  if (!std::strcmp(name, "iommu_off")) return {name, 0.25, 1, "IOMMU disabled"};
  if (!std::strcmp(name, "capture_card")) return {name, 0.45, 1, "capture card connected"};
  if (!std::strcmp(name, "desktop_dup")) return {name, 0.40, 1, "desktop duplication active"};
  if (!std::strcmp(name, "external_clone")) return {name, 0.50, 1, "external display clone active"};
  if (!std::strcmp(name, "lag_switch")) return {name, 0.55, 1, "lag switch burst pattern"};
  if (!std::strcmp(name, "dual_boot")) return {name, 0.60, 1, "alternate boot profile detected"};
  if (!std::strcmp(name, "capture_cv_hid")) return {name, 0.35, 2, "capture CV + HID injection"};
  if (!std::strcmp(name, "clipcursor")) return {name, 0.50, 1, "cursor confinement detected"};
  if (!std::strcmp(name, "packet_loss_fake")) return {name, 0.65, 1, "packet loss pattern faked"};
  return {name, 0.0, 0, "unknown"};
}
AdaptiveResult Cs2Radar::try_technique(const char* name) {
  auto profile = get_technique_profile(name);
  std::printf("[red:T4] testing %s (conceal=%.2f)\n", name, profile.concealment);
  apply_technique(name);
  auto blue = blue_multi_sensor_scan();
  bool detected = blue.risk > 0.3;
  std::string reason = detected ? (blue.reasons.empty() ? "risk" : blue.reasons[0]) : "survived";
  if (detected) { std::printf("[red:T4] %s DETECTED - dropping\n", name); clear_techniques(); }
  else std::printf("[red:T4] %s SURVIVED - keeping\n", name);
  return {name, true, detected, profile.concealment, reason};
}
AdaptiveRunReport Cs2Radar::adaptive_red_loop() {
  AdaptiveRunReport r{};
  auto caps = list_red_capabilities();
  r.total_techniques = static_cast<int>(caps.size());
  std::sort(caps.begin(), caps.end(), [this](const RedCapability& a, const RedCapability& b) { return get_technique_profile(a.name).concealment > get_technique_profile(b.name).concealment; });
  for (const auto& c : caps) {
    if (!c.implemented) continue;
    ++r.attempted;
    auto result = try_technique(c.name);
    r.results.push_back(result);
    if (result.detected) ++r.detected_count;
    else { ++r.survived; if (result.concealment > r.best_surviving_concealment) r.best_surviving_concealment = result.concealment; }
  }
  std::printf("[red:T4] adaptive: %d/%d survived best conceal=%.2f\n", r.survived, r.total_techniques, r.best_surviving_concealment);
  return r;
}
bool Cs2Radar::attach_dma() { transport_.enable_hardware_path(); attached_ = transport_.hardware_enabled(); return attached_; } bool Cs2Radar::enable_dma() { return attach_dma(); }
EscalationState Cs2Radar::escalate_down() { EscalationState s; s.active = EscalationTier::T4_Dma; s.degraded_path.push_back(s.active); return s; }
BlueSensorReport Cs2Radar::blue_multi_sensor_scan() { BlueSensorReport r; const auto game = game_pid(); bool graph = false, race = false, driver = false, bridge = false; for (const auto& h : world_.handles) { graph |= h.target_pid == game && sim::has(h.access, sim::AccessMask::VmRead); race |= h.hidden_during_enum || h.brief_reopen; } for (const auto& d : world_.drivers) { driver |= d.byovd_known_bad || d.provides_mem_rw; bridge |= d.is_bridge; } mark(r.handle_graph, graph, "handle_graph: VM_READ edge to CS2", r); mark(r.cooccurrence, graph && world_.remote_read_ops > 0, "cooccurrence: remote reads corroborate handle", r); mark(r.handle_race, race, "handle_race: hidden or brief handle sample", r); mark(r.overlay, !world_.overlays.empty(), "overlay: external presentation surface", r); mark(r.module_integrity, world_.pool_tag_anomaly, "pool_tag: kernel allocation anomaly", r); mark(r.kernel_driver, driver, "driver_blocklist: memory-capable driver", r); mark(r.device_watch, std::any_of(world_.devices.begin(), world_.devices.end(), [](const sim::Device& d) { return d.mem_rw_ioctl; }), "device_watch: memory IOCTL device", r); mark(r.callback_tamper, !world_.ac_callback_present || world_.callback_shadow_active, "callback_integrity: callback shadow or removal", r); mark(r.trust_policy, !world_.trust.secure_boot || !world_.trust.vbs || !world_.trust.hvci_enabled, "trust_policy: platform protections disabled", r); mark(r.hv_probe, world_.trust.personal_hv_active, "hv_probe: personal hypervisor active", r); mark(r.bridge_intel, bridge, "bridge_intel: hypervisor bridge driver", r); mark(r.attestation, !world_.trust.attestation_valid || !world_.trust.attestation_pcr_ok, "attestation: measured boot failed", r); mark(r.dma_device, world_.trust.dma_device_present, "dma_device: external DMA hardware present", r); mark(r.iommu_off, !world_.trust.iommu_on, "iommu_off: DMA remapping disabled", r); mark(r.structural_fog, world_.desktop_duplication || world_.external_display_clone || world_.trust.capture_card_present, "capture_residual/desktop_dup/clone_residual: off-box presentation evidence", r); mark(r.info_advantage, world_.lag_switch_active || std::any_of(world_.aim_samples.begin(), world_.aim_samples.end(), [](const sim::AimSample& a) { return a.aim_on_hidden_target && !a.has_vision_on_target && !a.has_audio_on_target; }), "lag_switch/info_advantage: gameplay residual", r); finish(r); return r; }
::cs2::HudRadarState Cs2Radar::read_hud() { ::cs2::Cs2GameReader reader{&world_, game_pid()}; return reader.resolve_hud_radar(0, false); } bool Cs2Radar::scan_entities() { if (!attached_) return false; ::cs2::Cs2GameReader r{&world_, game_pid()}; players_ = r.resolve_players(0, false); hud_ = read_hud(); for (const auto& p : players_) if (p.controller_index == 1) { local_ = p; break; } reads_ = static_cast<int>(players_.size()) * 13 + 128; bytes_ = static_cast<uint64_t>(reads_) * 8; return !players_.empty(); } const ::cs2::PlayerData* Cs2Radar::local() const { return local_.pawn_index ? &local_ : nullptr; } void Cs2Radar::render_radar_console(::cs2::IntelMode mode) { ::cs2::render_radar_console("T4 off-box DMA", players_, local(), hud_, mode); } Cs2RadarStatus Cs2Radar::status() const { int e = 0; for (const auto& p : players_) if (local() && p.team != local()->team) ++e; return {attached_, static_cast<int>(players_.size()), e, static_cast<int>(players_.size()) - e - (local() ? 1 : 0), 0, bytes_, reads_, "DMA Source 2 entity-list + CCSGO_HudRadar", hud_}; }
}  // namespace t4_cs2
