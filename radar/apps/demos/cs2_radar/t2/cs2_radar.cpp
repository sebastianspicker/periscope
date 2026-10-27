#include "cs2_radar.hpp"
#include "cs2/radar_console.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace t2_cs2 {
namespace { void mark(bool& b, bool hit, const char* why, BlueSensorReport& r) { b = hit; if (hit) r.reasons.emplace_back(why); } void finish(BlueSensorReport& r) { r.signal_count = static_cast<int>(r.reasons.size()); r.risk = std::min(r.signal_count * .25, 1.0); } }
std::vector<RedCapability> list_red_capabilities() {
  static const std::vector<RedCapability> caps{{"byovd_load", "Load a known-bad memory RW driver", true}, {"custom_driver", "Load a custom memory RW driver", true}, {"ioctl_read", "Expose a memory-read IOCTL device", true}, {"callback_strip", "Remove process callbacks", true}, {"callback_shadow", "Shadow callback lists during probes", true}, {"dkom_hide", "Hide reader from weak process enumeration", true}, {"physmem_map", "Open physical-memory mapping path", true}, {"pool_tag_hide", "Leave pool-tag anomaly", true}, {"minifilter_strip", "Remove minifilter callbacks", true}, {"registry_notify_strip", "Remove registry notifications", true}, {"etw_ti_blind", "Blind ETW Threat Intelligence", true}, {"scm_service", "Register a kernel service through SCM", true}};
  return caps;
}
void Cs2Radar::apply_technique(const char* name) {
  if (!name) return; const auto activate = [](bool& f, int& n) { if (!f) { f = true; ++n; } };
  if (!std::strcmp(name, "byovd_load")) { activate(techniques_.byovd_load, techniques_.active_count); world_.load_driver({"vulnmem.sys", "bad", "Lab", false, true, false, false, true}); }
  else if (!std::strcmp(name, "custom_driver")) { activate(techniques_.custom_driver, techniques_.active_count); world_.load_driver({"custom.sys", "custom", "Lab", false, false, false, false, true}); }
  else if (!std::strcmp(name, "ioctl_read")) { activate(techniques_.ioctl_read, techniques_.active_count); world_.create_device({"\\\\.\\MemRw", "custom.sys", true}); }
  else if (!std::strcmp(name, "callback_strip")) { activate(techniques_.callback_strip, techniques_.active_count); world_.process_notify = 0; world_.ac_callback_present = false; }
  else if (!std::strcmp(name, "callback_shadow")) { activate(techniques_.callback_shadow, techniques_.active_count); world_.callback_shadow_active = true; }
  else if (!std::strcmp(name, "dkom_hide")) { activate(techniques_.dkom_hide, techniques_.active_count); if (auto* p = world_.proc(pid())) p->hidden_from_weak_enum = true; }
  else if (!std::strcmp(name, "physmem_map")) { activate(techniques_.physmem_map, techniques_.active_count); world_.physmem_device_open = true; }
  else if (!std::strcmp(name, "pool_tag_hide")) { activate(techniques_.pool_tag_hide, techniques_.active_count); world_.pool_tag_anomaly = true; }
  else if (!std::strcmp(name, "minifilter_strip")) { activate(techniques_.minifilter_strip, techniques_.active_count); world_.minifilter_callbacks = 0; world_.minifilter_present = false; }
  else if (!std::strcmp(name, "registry_notify_strip")) { activate(techniques_.registry_notify_strip, techniques_.active_count); world_.registry_notify = 0; world_.registry_notify_present = false; }
  else if (!std::strcmp(name, "etw_ti_blind")) { activate(techniques_.etw_ti_blind, techniques_.active_count); world_.etw_ti_blind = true; }
  else if (!std::strcmp(name, "scm_service")) { activate(techniques_.scm_service, techniques_.active_count); world_.add_service({"MemRwSvc", "memrw.sys", true}); }
  else return; std::printf("[red:T2] applied %s\n", name);
}
void Cs2Radar::apply_all_techniques() { for (const auto& c : list_red_capabilities()) apply_technique(c.name); }
void Cs2Radar::clear_techniques() { world_.process_notify = world_.process_notify_true; world_.ac_callback_present = world_.ac_callback_true; world_.callback_shadow_active = false; world_.physmem_device_open = world_.pool_tag_anomaly = world_.etw_ti_blind = false; world_.minifilter_callbacks = 2; world_.minifilter_present = true; world_.registry_notify = 2; world_.registry_notify_present = true; world_.drivers.erase(std::remove_if(world_.drivers.begin(), world_.drivers.end(), [](const sim::Driver& d) { return d.name == "vulnmem.sys" || d.name == "custom.sys"; }), world_.drivers.end()); world_.devices.erase(std::remove_if(world_.devices.begin(), world_.devices.end(), [](const sim::Device& d) { return d.name == "\\\\.\\MemRw"; }), world_.devices.end()); world_.services.erase(std::remove_if(world_.services.begin(), world_.services.end(), [](const sim::ServiceEvent& s) { return s.name == "MemRwSvc"; }), world_.services.end()); if (auto* p = world_.proc(pid())) p->hidden_from_weak_enum = false; techniques_ = {}; }
TechniqueProfile Cs2Radar::get_technique_profile(const char* name) {
  if (!std::strcmp(name, "byovd_load")) return {name, 0.15, 3, "signed vulnerable driver loaded"};
  if (!std::strcmp(name, "custom_driver")) return {name, 0.25, 2, "unsigned driver loaded"};
  if (!std::strcmp(name, "ioctl_read")) return {name, 0.20, 2, "memrw IOCTL device created"};
  if (!std::strcmp(name, "callback_strip")) return {name, 0.50, 1, "AC process notify callback stripped"};
  if (!std::strcmp(name, "dkom_hide")) return {name, 0.40, 1, "process hidden via DKOM"};
  if (!std::strcmp(name, "physmem_map")) return {name, 0.35, 1, "physical memory mapping active"};
  if (!std::strcmp(name, "pool_tag_hide")) return {name, 0.55, 1, "pool tag anomalies hidden"};
  if (!std::strcmp(name, "callback_shadow")) return {name, 0.60, 1, "shadow callback restored on probe"};
  if (!std::strcmp(name, "etw_ti_blind")) return {name, 0.65, 1, "ETW-TI provider disabled"};
  if (!std::strcmp(name, "scm_service")) return {name, 0.30, 2, "SCM service registered"};
  return {name, 0.0, 0, "unknown"};
}
AdaptiveResult Cs2Radar::try_technique(const char* name) {
  auto profile = get_technique_profile(name);
  std::printf("[red:T2] testing %s (conceal=%.2f)\n", name, profile.concealment);
  apply_technique(name);
  auto blue = blue_multi_sensor_scan();
  bool detected = blue.risk > 0.3;
  std::string reason = detected ? (blue.reasons.empty() ? "risk" : blue.reasons[0]) : "survived";
  if (detected) { std::printf("[red:T2] %s DETECTED - dropping\n", name); clear_techniques(); }
  else std::printf("[red:T2] %s SURVIVED - keeping\n", name);
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
  std::printf("[red:T2] adaptive: %d/%d survived best conceal=%.2f\n", r.survived, r.total_techniques, r.best_surviving_concealment);
  return r;
}
bool Cs2Radar::attach_kernel() { attached_ = transport_.bring_up(t2_red::KernelPath::Byovd); return attached_; } bool Cs2Radar::bring_up_kernel() { return attach_kernel(); }
EscalationState Cs2Radar::escalate_down() { EscalationState s; s.active = EscalationTier::T2_Kernel; s.can_escalate_to[0] = s.can_escalate_to[1] = true; s.degraded_path.push_back(s.active); if (attached_ && !world_.ranked_access_denied) return s; const auto reader = pid() ? pid() : world_.spawn("cs2_radar_t2_fallback.exe"); if (world_.open_process(reader, game_pid(), sim::AccessMask::VmRead, true)) { s.active = EscalationTier::T1_Syscall; s.degraded_path.push_back(s.active); return s; } if (world_.open_process(reader, game_pid(), sim::AccessMask::VmRead, false)) { s.active = EscalationTier::T0_Rpm; s.degraded_path.push_back(s.active); } return s; }
BlueSensorReport Cs2Radar::blue_multi_sensor_scan() { BlueSensorReport r; const auto game = game_pid(); bool graph = false, race = false; for (const auto& h : world_.handles) { graph |= h.target_pid == game && sim::has(h.access, sim::AccessMask::VmRead); race |= h.hidden_during_enum || h.brief_reopen; } bool bad_driver = false, bridge = false; for (const auto& d : world_.drivers) { bad_driver |= d.byovd_known_bad || d.provides_mem_rw; bridge |= d.is_bridge; } bool bad_module = false; for (const auto& [_, p] : world_.processes) for (const auto& m : p.modules) bad_module |= m.headers_erased || m.text_hash != "clean" || m.iat_hooked || m.eat_hooked; mark(r.handle_graph, graph, "handle_graph: VM_READ edge to CS2", r); mark(r.cooccurrence, graph && world_.remote_read_ops > 0, "cooccurrence: handle and remote-read telemetry", r); mark(r.handle_race, race, "handle_race: hidden or brief handle sample", r); mark(r.overlay, !world_.overlays.empty(), "overlay: external presentation surface", r); mark(r.module_integrity, bad_module || world_.pool_tag_anomaly, "pool_tag/module_integrity: executable memory anomaly", r); mark(r.kernel_driver, bad_driver, "driver_blocklist: memory-capable or known-bad driver", r); mark(r.device_watch, std::any_of(world_.devices.begin(), world_.devices.end(), [](const sim::Device& d) { return d.mem_rw_ioctl; }), "device_watch: memory IOCTL device", r); mark(r.callback_tamper, !world_.ac_callback_present || world_.callback_shadow_active || world_.process_notify != world_.process_notify_true, "callback_integrity: callback state differs from truth", r); mark(r.structural_fog, world_.physmem_device_open || world_.etw_ti_blind, "physmem_map/etw_ti: privileged telemetry residual", r); finish(r); return r; }
::cs2::HudRadarState Cs2Radar::read_hud() { ::cs2::Cs2GameReader reader{&world_, game_pid()}; return reader.resolve_hud_radar(pid(), false); } bool Cs2Radar::scan_entities() { if (!attached_) return false; ::cs2::Cs2GameReader r{&world_, game_pid()}; players_ = r.resolve_players(pid(), false); hud_ = read_hud(); for (const auto& p : players_) if (p.controller_index == 1) { local_ = p; break; } reads_ = static_cast<int>(players_.size()) * 13 + 128; bytes_ = static_cast<uint64_t>(reads_) * 8; return !players_.empty(); } const ::cs2::PlayerData* Cs2Radar::local() const { return local_.pawn_index ? &local_ : nullptr; } void Cs2Radar::render_radar_console(::cs2::IntelMode mode) { ::cs2::render_radar_console("T2 kernel/BYOVD", players_, local(), hud_, mode); } Cs2RadarStatus Cs2Radar::status() const { int e = 0; for (const auto& p : players_) if (local() && p.team != local()->team) ++e; return {attached_, static_cast<int>(players_.size()), e, static_cast<int>(players_.size()) - e - (local() ? 1 : 0), 0, bytes_, reads_, "Kernel/BYOVD Source 2 entity-list + CCSGO_HudRadar", hud_}; }
}  // namespace t2_cs2
