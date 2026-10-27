#include "cs2_radar.hpp"
#include "cs2/radar_console.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace t1_cs2 {
namespace { void mark(bool& b, bool hit, const char* why, BlueSensorReport& r) { b = hit; if (hit) r.reasons.emplace_back(why); } void finish(BlueSensorReport& r) { r.signal_count = static_cast<int>(r.reasons.size()); r.risk = std::min(r.signal_count * .25, 1.0); } }
std::vector<RedCapability> list_red_capabilities() {
  static const std::vector<RedCapability> caps{{"direct_syscall", "Open VM_READ through the syscall path", true}, {"staged_loader", "Manual-map staged private RX content", true}, {"stack_spoof", "Spoof read stack origin", true}, {"etw_blind", "Blind ETW Threat Intelligence", true}, {"process_hollow", "Hollow a svchost host", true}, {"parent_lineage_spoof", "Spoof explorer parent lineage", true}, {"module_stomp", "Stomp module text and erase headers", true}, {"hwid_spoof", "Replace hardware identifier", true}, {"mapper_artifact", "Leave mapper-process artifact", true}, {"thread_hide", "Hide thread and spoof PEB debug flag", true}};
  return caps;
}
void Cs2Radar::apply_technique(const char* name) {
  if (!name) return; const auto activate = [](bool& f, int& n) { if (!f) { f = true; ++n; } };
  if (!std::strcmp(name, "direct_syscall")) { activate(techniques_.direct_syscall, techniques_.active_count); world_.open_process(pid_, game_pid_, sim::AccessMask::VmRead, true); if (!world_.handles.empty()) world_.handles.back().via_syscall_path = true; }
  else if (!std::strcmp(name, "staged_loader")) { activate(techniques_.staged_loader, techniques_.active_count); if (auto* p = world_.proc(pid_)) { p->manual_mapped_region = true; p->modules.push_back({"stage_rx", 0x50000000, 0x1000, false}); } }
  else if (!std::strcmp(name, "stack_spoof")) { activate(techniques_.stack_spoof, techniques_.active_count); world_.stack_spoof_on_read = true; }
  else if (!std::strcmp(name, "etw_blind")) { activate(techniques_.etw_blind, techniques_.active_count); world_.etw_enabled = false; world_.etw_ti_blind = true; }
  else if (!std::strcmp(name, "process_hollow")) { activate(techniques_.process_hollow, techniques_.active_count); if (auto* p = world_.proc(pid_)) { p->hollowed = true; p->original_image = "svchost.exe"; } }
  else if (!std::strcmp(name, "parent_lineage_spoof")) { activate(techniques_.parent_lineage_spoof, techniques_.active_count); const auto explorer = world_.spawn("explorer.exe"); if (auto* p = world_.proc(pid_)) { p->parent_pid = explorer; p->looks_reputable = true; } }
  else if (!std::strcmp(name, "module_stomp")) { activate(techniques_.module_stomp, techniques_.active_count); if (auto* p = world_.proc(pid_)) { if (p->modules.empty()) p->modules.push_back({"kernel32.dll", 0x60000000, 0x1000}); auto& m = p->modules.front(); m.text_hash = "stomped"; m.headers_erased = true; } }
  else if (!std::strcmp(name, "hwid_spoof")) { activate(techniques_.hwid_spoof, techniques_.active_count); world_.spoof_hwid("disk:FAKE|mac:FAKE|smbios:FAKE"); }
  else if (!std::strcmp(name, "mapper_artifact")) { activate(techniques_.mapper_artifact, techniques_.active_count); world_.mapper_process_present = true; }
  else if (!std::strcmp(name, "thread_hide")) { activate(techniques_.thread_hide, techniques_.active_count); world_.thread_hide_from_debugger = true; world_.peb_being_debugged_spoofed = true; }
  else return; std::printf("[red:T1] applied %s\n", name);
}
void Cs2Radar::apply_all_techniques() { for (const auto& c : list_red_capabilities()) apply_technique(c.name); }
void Cs2Radar::clear_techniques() { world_.close_handles_from(pid_); world_.stack_spoof_on_read = false; world_.etw_enabled = true; world_.etw_ti_blind = false; world_.mapper_process_present = false; world_.thread_hide_from_debugger = false; world_.peb_being_debugged_spoofed = false; world_.trust.hwid = "disk:AAA|mac:BBB|smbios:CCC"; if (auto* p = world_.proc(pid_)) { p->manual_mapped_region = p->hollowed = p->looks_reputable = false; p->original_image.clear(); p->parent_pid = 0; p->modules.clear(); } techniques_ = {}; }
TechniqueProfile Cs2Radar::get_technique_profile(const char* name) {
  if (!std::strcmp(name, "direct_syscall")) return {name, 0.15, 2, "VM_READ handle via syscall path"};
  if (!std::strcmp(name, "staged_loader")) return {name, 0.40, 3, "multi-stage loader with private RX"};
  if (!std::strcmp(name, "stack_spoof")) return {name, 0.45, 1, "spoofed return address on read"};
  if (!std::strcmp(name, "etw_blind")) return {name, 0.55, 1, "ETW/ETW-TI disabled"};
  if (!std::strcmp(name, "process_hollow")) return {name, 0.35, 2, "hollowed process image"};
  if (!std::strcmp(name, "parent_lineage_spoof")) return {name, 0.50, 1, "spoofed parent PID"};
  if (!std::strcmp(name, "module_stomp")) return {name, 0.40, 2, "trusted module text overwritten"};
  if (!std::strcmp(name, "hwid_spoof")) return {name, 0.60, 1, "spoofed disk/mac/smbios"};
  if (!std::strcmp(name, "mapper_artifact")) return {name, 0.30, 1, "mapper process artifact"};
  if (!std::strcmp(name, "thread_hide")) return {name, 0.50, 1, "thread hidden from debugger"};
  return {name, 0.0, 0, "unknown"};
}
AdaptiveResult Cs2Radar::try_technique(const char* name) {
  auto profile = get_technique_profile(name);
  std::printf("[red:T1] testing %s (conceal=%.2f)\n", name, profile.concealment);
  apply_technique(name);
  auto blue = blue_multi_sensor_scan();
  bool detected = blue.risk > 0.3;
  std::string reason = detected ? (blue.reasons.empty() ? "risk" : blue.reasons[0]) : "survived";
  if (detected) {
    std::printf("[red:T1] %s DETECTED - dropping\n", name);
    clear_techniques();
  } else {
    std::printf("[red:T1] %s SURVIVED - keeping\n", name);
  }
  return {name, true, detected, profile.concealment, reason};
}
AdaptiveRunReport Cs2Radar::adaptive_red_loop() {
  AdaptiveRunReport r{};
  auto caps = list_red_capabilities();
  r.total_techniques = static_cast<int>(caps.size());
  std::sort(caps.begin(), caps.end(), [this](const RedCapability& a, const RedCapability& b) {
    return get_technique_profile(a.name).concealment > get_technique_profile(b.name).concealment;
  });
  for (const auto& c : caps) {
    if (!c.implemented) continue;
    ++r.attempted;
    auto result = try_technique(c.name);
    r.results.push_back(result);
    if (result.detected) ++r.detected_count;
    else { ++r.survived; if (result.concealment > r.best_surviving_concealment) r.best_surviving_concealment = result.concealment; }
  }
  std::printf("[red:T1] adaptive: %d/%d survived best conceal=%.2f\n", r.survived, r.total_techniques, r.best_surviving_concealment);
  return r;
}
bool Cs2Radar::attach_syscall() { pid_ = world_.spawn("cs2_radar_t1.exe"); game_pid_ = world_.game_pid(); attached_ = backend_.attach_world(world_, pid_, game_pid_, true) == ac::Status::Ok; return attached_; }
EscalationState Cs2Radar::escalate_down() { EscalationState s; s.active = EscalationTier::T1_Syscall; s.can_escalate_to[0] = s.can_escalate_to[2] = true; s.degraded_path.push_back(s.active); if (attached_ && !world_.ranked_access_denied) return s; if (!pid_) pid_ = world_.spawn("cs2_radar_t1_fallback.exe"); game_pid_ = world_.game_pid(); if (world_.open_process(pid_, game_pid_, sim::AccessMask::VmRead, false)) { s.active = EscalationTier::T0_Rpm; s.degraded_path.push_back(s.active); return s; } world_.load_driver({"fallback_memrw.sys", "lab_fallback", "Lab", false, false, false, false, true, 20}); world_.create_device({"\\\\.\\FallbackMemRw", "fallback_memrw.sys", true}); std::vector<std::uint8_t> bytes; if (const auto* g = world_.proc(game_pid_); g && world_.device_ioctl_read(pid_, "\\\\.\\FallbackMemRw", game_pid_, g->base, 4, bytes)) { s.active = EscalationTier::T2_Kernel; s.degraded_path.push_back(s.active); } return s; }
BlueSensorReport Cs2Radar::blue_multi_sensor_scan() { BlueSensorReport r; const auto game = world_.game_pid(); bool graph = false, race = false; for (const auto& h : world_.handles) { graph |= h.target_pid == game && sim::has(h.access, sim::AccessMask::VmRead); race |= h.hidden_during_enum || h.brief_reopen; } mark(r.handle_graph, graph, "handle_truth: kernel handle table observes syscall handle", r); mark(r.cooccurrence, !world_.sections.empty() || (graph && world_.proc(pid_) && world_.proc(pid_)->reader_active), "staging/cooccurrence: section or active reader", r); mark(r.handle_race, race || world_.etw_ti_blind, "handle_race/etw_blind: transient handle or TI disabled", r); mark(r.overlay, !world_.overlays.empty() || world_.stack_spoof_on_read, "overlay/stack_spoof: presentation or read-origin inconsistency", r); mark(r.module_integrity, world_.proc(pid_) && world_.proc(pid_)->parent_pid != 0 && world_.proc(pid_)->parent_pid != world_.ac_pid(), "parent_lineage: unexpected reader parent", r); finish(r); return r; }
::cs2::HudRadarState Cs2Radar::read_hud() { ::cs2::Cs2GameReader reader{&world_, game_pid_}; return reader.resolve_hud_radar(pid_, true); }
bool Cs2Radar::scan_entities() { if (!attached_) return false; ::cs2::Cs2GameReader r{&world_, game_pid_}; players_ = r.resolve_players(pid_, true); hud_ = read_hud(); for (const auto& p : players_) if (p.controller_index == 1) { local_ = p; break; } reads_ = static_cast<int>(players_.size()) * 13 + 128; bytes_ = static_cast<uint64_t>(reads_) * 8; return !players_.empty(); }
const ::cs2::PlayerData* Cs2Radar::local() const { return local_.pawn_index ? &local_ : nullptr; } void Cs2Radar::render_radar_console(::cs2::IntelMode mode) { ::cs2::render_radar_console("T1 syscall RPM", players_, local(), hud_, mode); } Cs2RadarStatus Cs2Radar::status() const { int e = 0; for (const auto& p : players_) if (local() && p.team != local()->team) ++e; return {attached_, static_cast<int>(players_.size()), e, static_cast<int>(players_.size()) - e - (local() ? 1 : 0), static_cast<int>(backend_.handle_count()), bytes_, reads_, "Direct-syscall Source 2 entity-list + CCSGO_HudRadar", hud_}; }
}  // namespace t1_cs2
