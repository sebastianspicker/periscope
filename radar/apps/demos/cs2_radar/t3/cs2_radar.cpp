#include "cs2_radar.hpp"
#include "cs2/radar_console.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace t3_cs2 {
namespace { void mark(bool& b, bool hit, const char* why, BlueSensorReport& r) { b = hit; if (hit) r.reasons.emplace_back(why); } void finish(BlueSensorReport& r) { r.signal_count = static_cast<int>(r.reasons.size()); r.risk = std::min(r.signal_count * .25, 1.0); } }
std::vector<RedCapability> list_red_capabilities() {
  static const std::vector<RedCapability> caps{{"personal_hv", "Start a personal hypervisor after disabling VBS", true}, {"ept_hide", "Hide AC pages with EPT", true}, {"timing_spoof", "Spoof hypervisor timing", true}, {"cr3_stealth", "Use a bridge driver for CR3-stealth reads", true}, {"attestation_fail", "Invalidate measured-boot attestation", true}, {"efi_boot_entry", "Add an unexpected EFI entry", true}, {"dual_view_dirty", "Dirty the secure-kernel view", true}, {"feature_control_msr", "Spoof feature-control MSR", true}, {"infinity_hook", "Leave InfinityHook residual", true}, {"hvci_race", "Disable HVCI through a race", true}, {"nested_hv", "Run personal HV under platform HV", true}, {"elam_bypass", "Disable ELAM", true}};
  return caps;
}
void Cs2Radar::apply_technique(const char* name) {
  if (!name) return; const auto activate = [](bool& f, int& n) { if (!f) { f = true; ++n; } };
  if (!std::strcmp(name, "personal_hv")) { activate(techniques_.personal_hv, techniques_.active_count); world_.trust.vbs = false; world_.trust.hvci = false; world_.try_start_personal_hv("HV"); }
  else if (!std::strcmp(name, "ept_hide")) { activate(techniques_.ept_hide, techniques_.active_count); world_.trust.ept_hide_ac_pages = true; }
  else if (!std::strcmp(name, "timing_spoof")) { activate(techniques_.timing_spoof, techniques_.active_count); world_.trust.timing_spoofed = true; }
  else if (!std::strcmp(name, "cr3_stealth")) { activate(techniques_.cr3_stealth, techniques_.active_count); if (!world_.trust.personal_hv_active) { world_.trust.vbs = false; world_.try_start_personal_hv("HV"); } world_.load_driver({"hvbridge.sys", "bridge", "Lab", false, false, false, true, true}); }
  else if (!std::strcmp(name, "attestation_fail")) { activate(techniques_.attestation_fail, techniques_.active_count); world_.trust.attestation_valid = false; }
  else if (!std::strcmp(name, "efi_boot_entry")) { activate(techniques_.efi_boot_entry, techniques_.active_count); world_.trust.unexpected_efi_entry = true; world_.trust.efi_entry_name = "HV"; }
  else if (!std::strcmp(name, "dual_view_dirty")) { activate(techniques_.dual_view_dirty, techniques_.active_count); world_.trust.secure_kernel_view_dirty = true; }
  else if (!std::strcmp(name, "feature_control_msr")) { activate(techniques_.feature_control_msr, techniques_.active_count); world_.feature_control_spoofed = true; }
  else if (!std::strcmp(name, "infinity_hook")) { activate(techniques_.infinity_hook, techniques_.active_count); world_.infinity_hook_residual = true; }
  else if (!std::strcmp(name, "hvci_race")) { activate(techniques_.hvci_race, techniques_.active_count); world_.trust.hvci_enabled = false; }
  else if (!std::strcmp(name, "nested_hv")) { activate(techniques_.nested_hv, techniques_.active_count); world_.trust.platform_hv_active = true; world_.trust.vbs = false; world_.try_start_personal_hv("HV"); }
  else if (!std::strcmp(name, "elam_bypass")) { activate(techniques_.elam_bypass, techniques_.active_count); world_.trust.elam_enabled = false; }
  else return; std::printf("[red:T3] applied %s\n", name);
}
void Cs2Radar::apply_all_techniques() { for (const auto& c : list_red_capabilities()) apply_technique(c.name); }
void Cs2Radar::clear_techniques() { world_.trust.vbs = world_.trust.hvci = world_.trust.hvci_enabled = true; world_.trust.personal_hv_active = false; world_.trust.ept_hide_ac_pages = world_.trust.timing_spoofed = world_.trust.secure_kernel_view_dirty = false; world_.trust.attestation_valid = true; world_.trust.unexpected_efi_entry = false; world_.trust.efi_entry_name.clear(); world_.trust.platform_hv_active = false; world_.trust.elam_enabled = true; world_.feature_control_spoofed = world_.infinity_hook_residual = false; world_.drivers.erase(std::remove_if(world_.drivers.begin(), world_.drivers.end(), [](const sim::Driver& d) { return d.name == "hvbridge.sys"; }), world_.drivers.end()); techniques_ = {}; }
TechniqueProfile Cs2Radar::get_technique_profile(const char* name) {
  if (!std::strcmp(name, "personal_hv")) return {name, 0.10, 4, "personal hypervisor active"};
  if (!std::strcmp(name, "ept_hide")) return {name, 0.55, 1, "EPT page hiding active"};
  if (!std::strcmp(name, "timing_spoof")) return {name, 0.60, 1, "timing measurements spoofed"};
  if (!std::strcmp(name, "cr3_stealth")) return {name, 0.50, 1, "CR3 targeting active"};
  if (!std::strcmp(name, "attestation_bypass") || !std::strcmp(name, "attestation_fail")) return {name, 0.65, 1, "attestation checks bypassed"};
  if (!std::strcmp(name, "efi_boot_entry")) return {name, 0.45, 1, "unexpected EFI boot entry"};
  if (!std::strcmp(name, "dual_view_dirty")) return {name, 0.50, 1, "secure kernel view dirty"};
  if (!std::strcmp(name, "feature_control_msr")) return {name, 0.55, 1, "MSR feature control spoofed"};
  if (!std::strcmp(name, "infinity_hook")) return {name, 0.40, 1, "InfinityHook residual present"};
  if (!std::strcmp(name, "hvci_race")) return {name, 0.60, 1, "HVCI race condition exploited"};
  if (!std::strcmp(name, "nested_hv")) return {name, 0.35, 2, "nested hypervisor deployment"};
  return {name, 0.0, 0, "unknown"};
}
AdaptiveResult Cs2Radar::try_technique(const char* name) {
  auto profile = get_technique_profile(name);
  std::printf("[red:T3] testing %s (conceal=%.2f)\n", name, profile.concealment);
  apply_technique(name);
  auto blue = blue_multi_sensor_scan();
  bool detected = blue.risk > 0.3;
  std::string reason = detected ? (blue.reasons.empty() ? "risk" : blue.reasons[0]) : "survived";
  if (detected) { std::printf("[red:T3] %s DETECTED - dropping\n", name); clear_techniques(); }
  else std::printf("[red:T3] %s SURVIVED - keeping\n", name);
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
  std::printf("[red:T3] adaptive: %d/%d survived best conceal=%.2f\n", r.survived, r.total_techniques, r.best_surviving_concealment);
  return r;
}
bool Cs2Radar::attach_hypervisor() { transport_.force_disable_vbs_for_lab(); attached_ = transport_.try_hv("ACLABHV") && transport_.open_bridge(); if (attached_) transport_.attach_best(); return attached_; } bool Cs2Radar::start_hv() { return attach_hypervisor(); }
EscalationState Cs2Radar::escalate_down() { EscalationState s; s.active = EscalationTier::T3_Hv; s.can_escalate_to[0] = s.can_escalate_to[1] = s.can_escalate_to[2] = true; s.degraded_path.push_back(s.active); if (attached_ && !world_.ranked_access_denied) return s; const auto reader = pid() ? pid() : world_.spawn("cs2_radar_t3_fallback.exe"); world_.load_driver({"fallback_memrw.sys", "lab_fallback", "Lab", false, false, false, false, true, 20}); world_.create_device({"\\\\.\\FallbackMemRw", "fallback_memrw.sys", true}); std::vector<std::uint8_t> bytes; if (const auto* g = world_.proc(game_pid()); g && world_.device_ioctl_read(reader, "\\\\.\\FallbackMemRw", game_pid(), g->base, 4, bytes)) { s.active = EscalationTier::T2_Kernel; s.degraded_path.push_back(s.active); return s; } if (world_.open_process(reader, game_pid(), sim::AccessMask::VmRead, true)) { s.active = EscalationTier::T1_Syscall; s.degraded_path.push_back(s.active); return s; } if (world_.open_process(reader, game_pid(), sim::AccessMask::VmRead, false)) { s.active = EscalationTier::T0_Rpm; s.degraded_path.push_back(s.active); } return s; }
BlueSensorReport Cs2Radar::blue_multi_sensor_scan() { BlueSensorReport r; const auto game = game_pid(); bool graph = false, race = false, driver = false, bridge = false; for (const auto& h : world_.handles) { graph |= h.target_pid == game && sim::has(h.access, sim::AccessMask::VmRead); race |= h.hidden_during_enum || h.brief_reopen; } for (const auto& d : world_.drivers) { driver |= d.byovd_known_bad || d.provides_mem_rw; bridge |= d.is_bridge; } mark(r.handle_graph, graph, "handle_graph: VM_READ edge to CS2", r); mark(r.cooccurrence, graph && world_.remote_read_ops > 0, "cooccurrence: remote reads corroborate handle", r); mark(r.handle_race, race, "handle_race: hidden or brief handle sample", r); mark(r.overlay, !world_.overlays.empty(), "overlay: external presentation surface", r); mark(r.module_integrity, world_.pool_tag_anomaly, "pool_tag: kernel allocation anomaly", r); mark(r.kernel_driver, driver, "driver_blocklist: memory-capable driver", r); mark(r.device_watch, std::any_of(world_.devices.begin(), world_.devices.end(), [](const sim::Device& d) { return d.mem_rw_ioctl; }), "device_watch: memory IOCTL device", r); mark(r.callback_tamper, !world_.ac_callback_present || world_.callback_shadow_active, "callback_integrity: callback shadow or removal", r); mark(r.trust_policy, !world_.trust.secure_boot || !world_.trust.vbs || !world_.trust.hvci_enabled, "trust_policy: platform protections disabled", r); mark(r.hv_probe, world_.trust.personal_hv_active || world_.trust.cpuid_latency_ns > world_.trust.baseline_latency_ns * 2, "hv_probe: personal hypervisor timing signature", r); mark(r.bridge_intel, bridge, "bridge_intel: hypervisor bridge driver", r); mark(r.attestation, !world_.trust.attestation_valid || !world_.trust.attestation_pcr_ok, "attestation: measured boot failed", r); mark(r.structural_fog, world_.trust.ept_hide_ac_pages || world_.trust.secure_kernel_view_dirty, "ept_dual_view/sk_dirty: secure-kernel mismatch", r); finish(r); return r; }
::cs2::HudRadarState Cs2Radar::read_hud() { ::cs2::Cs2GameReader reader{&world_, game_pid()}; return reader.resolve_hud_radar(pid(), false); } bool Cs2Radar::scan_entities() { if (!attached_) return false; ::cs2::Cs2GameReader r{&world_, game_pid()}; players_ = r.resolve_players(pid(), false); hud_ = read_hud(); for (const auto& p : players_) if (p.controller_index == 1) { local_ = p; break; } reads_ = static_cast<int>(players_.size()) * 13 + 128; bytes_ = static_cast<uint64_t>(reads_) * 8; return !players_.empty(); } const ::cs2::PlayerData* Cs2Radar::local() const { return local_.pawn_index ? &local_ : nullptr; } void Cs2Radar::render_radar_console(::cs2::IntelMode mode) { ::cs2::render_radar_console("T3 hypervisor", players_, local(), hud_, mode); } Cs2RadarStatus Cs2Radar::status() const { int e = 0; for (const auto& p : players_) if (local() && p.team != local()->team) ++e; return {attached_, static_cast<int>(players_.size()), e, static_cast<int>(players_.size()) - e - (local() ? 1 : 0), 0, bytes_, reads_, "HV Source 2 entity-list + CCSGO_HudRadar", hud_}; }
}  // namespace t3_cs2
