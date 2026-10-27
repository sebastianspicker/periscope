#include "cs2_radar.hpp"

#include "cs2/radar_console.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace t0_cs2 {
namespace {
void mark(bool& sensor, bool triggered, const char* reason, BlueSensorReport& report) {
  sensor = triggered;
  if (triggered) report.reasons.emplace_back(reason);
}
void finish(BlueSensorReport& report) {
  report.signal_count = static_cast<int>(report.reasons.size());
  report.risk = std::min(report.signal_count * 0.25, 1.0);
}
}  // namespace

Cs2Radar::Cs2Radar(sim::World& world) : world_(world) {}
std::vector<RedCapability> list_red_capabilities() {
  static const std::vector<RedCapability> caps{{"openprocess_rpm", "OpenProcess VM_READ and ReadProcessMemory", true}, {"handle_minimize", "Brief VM_READ-only handle", true}, {"handle_hide_on_enum", "Hide handle during enumeration", true}, {"process_rename", "Masquerade as a reputable overlay", true}, {"section_map", "Share entity bytes through a section", true}, {"gdi_bitblt_capture", "Capture the game with GDI BitBlt", true}, {"proxy_handle", "Use a reputable process as handle proxy", true}, {"throttled_sparse_reads", "Reduce remote-read cadence", true}};
  return caps;
}
void Cs2Radar::apply_technique(const char* name) {
  if (!name) return;
  const auto activate = [](bool& flag, int& count) { if (!flag) { flag = true; ++count; } };
  if (!std::strcmp(name, "openprocess_rpm")) { activate(techniques_.openprocess_rpm, techniques_.active_count); world_.open_process(pid_, game_pid_, sim::AccessMask::VmRead, false); if (auto* game = world_.proc(game_pid_)) world_.read_mem(pid_, game_pid_, game->base, 4, true); }
  else if (!std::strcmp(name, "handle_minimize")) { activate(techniques_.handle_minimize, techniques_.active_count); world_.open_process(pid_, game_pid_, sim::AccessMask::VmRead, false); if (!world_.handles.empty()) { auto& h = world_.handles.back(); h.access = sim::AccessMask::VmRead; h.brief_reopen = true; } }
  else if (!std::strcmp(name, "handle_hide_on_enum")) { activate(techniques_.handle_hide_on_enum, techniques_.active_count); world_.open_process(pid_, game_pid_, sim::AccessMask::VmRead, false); if (!world_.handles.empty()) world_.handles.back().hidden_during_enum = true; }
  else if (!std::strcmp(name, "process_rename")) { activate(techniques_.process_rename, techniques_.active_count); if (auto* p = world_.proc(pid_)) { p->name = "nvidia-overlay.exe"; p->looks_reputable = true; } }
  else if (!std::strcmp(name, "section_map")) { activate(techniques_.section_map, techniques_.active_count); world_.add_section({"entity_data", pid_, game_pid_, true}); }
  else if (!std::strcmp(name, "gdi_bitblt_capture")) { activate(techniques_.gdi_bitblt_capture, techniques_.active_count); world_.gdi_bitblt_capture = true; }
  else if (!std::strcmp(name, "proxy_handle")) { activate(techniques_.proxy_handle, techniques_.active_count); const auto proxy = world_.spawn("nvcontainer.exe"); if (auto* p = world_.proc(proxy)) p->looks_reputable = true; world_.open_process(proxy, game_pid_, sim::AccessMask::VmRead, false); if (!world_.handles.empty()) { auto& h = world_.handles.back(); h.via_proxy = true; } world_.handle_proxy_active = true; world_.handle_proxy_owner_pid = proxy; world_.handle_proxy_consumer_pid = pid_; }
  else if (!std::strcmp(name, "throttled_sparse_reads")) { activate(techniques_.throttled_sparse_reads, techniques_.active_count); world_.remote_read_ops = std::min(world_.remote_read_ops, 2u); world_.note("red throttle_hz=2 sparse reads"); }
  else return;
  std::printf("[red:T0] applied %s\n", name);
}
void Cs2Radar::apply_all_techniques() { for (const auto& c : list_red_capabilities()) apply_technique(c.name); }
void Cs2Radar::clear_techniques() { world_.close_handles_from(pid_); world_.sections.erase(std::remove_if(world_.sections.begin(), world_.sections.end(), [this](const sim::SharedSection& s) { return s.creator_pid == pid_ && s.name == "entity_data"; }), world_.sections.end()); world_.gdi_bitblt_capture = false; world_.handle_proxy_active = false; world_.handle_proxy_owner_pid = world_.handle_proxy_consumer_pid = 0; world_.remote_read_ops = 0; if (auto* p = world_.proc(pid_)) { p->name = "cs2_radar_t0.exe"; p->looks_reputable = false; } techniques_ = {}; }
TechniqueProfile Cs2Radar::get_technique_profile(const char* name) {
  if (!std::strcmp(name, "openprocess_rpm")) return {name, 0.10, 2, "VM_READ handle on cs2.exe"};
  if (!std::strcmp(name, "handle_minimize")) return {name, 0.25, 2, "brief VM_READ handle with narrow rights"};
  if (!std::strcmp(name, "handle_hide_on_enum")) return {name, 0.30, 1, "handle hidden during system enumeration"};
  if (!std::strcmp(name, "process_rename")) return {name, 0.20, 1, "process masquerading as nvidia-overlay"};
  if (!std::strcmp(name, "section_map")) return {name, 0.50, 1, "shared section between reader and game"};
  if (!std::strcmp(name, "gdi_bitblt_capture")) return {name, 0.70, 1, "GDI BitBlt screen capture (no handle)"};
  if (!std::strcmp(name, "proxy_handle")) return {name, 0.45, 3, "handle owned by reputable nvcontainer.exe"};
  if (!std::strcmp(name, "throttled_sparse_reads")) return {name, 0.35, 1, "read cadence throttled to reduce telemetry"};
  return {name, 0.0, 0, "unknown technique"};
}
AdaptiveResult Cs2Radar::try_technique(const char* name) {
  auto profile = get_technique_profile(name);
  world_.note(std::string("red:testing ") + name + " conceal=" + std::to_string(profile.concealment));
  std::printf("[red:T0] testing %s (concealment=%.2f scars=%d %s)\n", name, profile.concealment, profile.scar_count, profile.scar_detail);
  apply_technique(name);
  auto blue = blue_multi_sensor_scan();
  bool detected = blue.risk > 0.3;
  std::string reason = detected ? (blue.reasons.empty() ? "risk threshold" : blue.reasons[0]) : "survived";
  if (detected) {
    world_.note(std::string("red:DROPPING ") + name + " — blue detected: " + reason);
    std::printf("[red:T0] %s DETECTED by blue (%s) — dropping\n", name, reason.c_str());
    clear_techniques();
    if (attached_) apply_technique("openprocess_rpm");
  } else {
    world_.note(std::string("red:KEEPING ") + name + " — blue missed it");
    std::printf("[red:T0] %s SURVIVED — keeping active\n", name);
  }
  return {name, true, detected, profile.concealment, reason};
}
AdaptiveRunReport Cs2Radar::adaptive_red_loop() {
  AdaptiveRunReport report{};
  auto caps = list_red_capabilities();
  report.total_techniques = static_cast<int>(caps.size());
  std::sort(caps.begin(), caps.end(), [](const RedCapability& a, const RedCapability& b) {
    TechniqueProfile pa{nullptr,0,0,nullptr}, pb{nullptr,0,0,nullptr};
    pa = (a.implemented ? TechniqueProfile{a.name,0,0,""} : pa);
    pb = (b.implemented ? TechniqueProfile{b.name,0,0,""} : pb);
    (void)pa; (void)pb;
    return false; // sort by concealment below
  });
  std::sort(caps.begin(), caps.end(),
    [this](const RedCapability& a, const RedCapability& b) {
      return get_technique_profile(a.name).concealment > get_technique_profile(b.name).concealment;
    });
  for (const auto& cap : caps) {
    if (!cap.implemented) continue;
    ++report.attempted;
    auto result = try_technique(cap.name);
    report.results.push_back(result);
    if (result.detected) {
      ++report.detected_count;
    } else {
      ++report.survived;
      if (result.concealment > report.best_surviving_concealment)
        report.best_surviving_concealment = result.concealment;
    }
  }
  std::printf("[red:T0] adaptive complete: %d/%d survived (best conceal=%.2f)\n",
              report.survived, report.total_techniques, report.best_surviving_concealment);
  return report;
}
bool Cs2Radar::attach_rpm() {
  pid_ = world_.spawn("cs2_radar_t0.exe"); game_pid_ = world_.game_pid();
  attached_ = backend_.attach_world(world_, pid_, game_pid_, false) == ac::Status::Ok;
  return attached_;
}
EscalationState Cs2Radar::escalate_down() {
  EscalationState state; state.active = EscalationTier::T0_Rpm;
  state.can_escalate_to[1] = true; state.can_escalate_to[2] = true;
  state.degraded_path.push_back(EscalationTier::T0_Rpm);
  if (attached_ && !world_.ranked_access_denied) return state;
  if (pid_ == 0) pid_ = world_.spawn("cs2_radar_t0_fallback.exe");
  game_pid_ = world_.game_pid();
  if (world_.open_process(pid_, game_pid_, sim::AccessMask::VmRead, true)) {
    state.active = EscalationTier::T1_Syscall;
    state.degraded_path.push_back(state.active);
    return state;
  }
  world_.load_driver({"fallback_memrw.sys", "lab_fallback", "Lab", false, false, false, false, true, 20});
  world_.create_device({"\\\\.\\FallbackMemRw", "fallback_memrw.sys", true});
  std::vector<std::uint8_t> bytes;
  if (const auto* game = world_.proc(game_pid_);
      game && world_.device_ioctl_read(pid_, "\\\\.\\FallbackMemRw", game_pid_, game->base, 4, bytes)) {
    state.active = EscalationTier::T2_Kernel;
    state.degraded_path.push_back(state.active);
  }
  return state;
}
BlueSensorReport Cs2Radar::blue_multi_sensor_scan() {
  BlueSensorReport report;
  const auto game = world_.game_pid();
  bool graph = false, race = false;
  for (const auto& h : world_.handles) { graph |= h.target_pid == game && sim::has(h.access, sim::AccessMask::VmRead); race |= h.hidden_during_enum || h.brief_reopen; }
  mark(report.handle_graph, graph, "handle_graph: VM_READ edge to CS2", report);
  mark(report.cooccurrence, graph && world_.proc(pid_) && world_.proc(pid_)->reader_active, "cooccurrence: reader active with game handle", report);
  mark(report.handle_race, race, "handle_race: hidden or brief handle sample", report);
  mark(report.overlay, !world_.overlays.empty(), "overlay: external presentation surface", report);
  finish(report); return report;
}
bool Cs2Radar::scan_entities() { if (!attached_) return false; ::cs2::Cs2GameReader reader{&world_, game_pid_}; players_ = reader.resolve_players(pid_, true); hud_ = read_hud(); for (const auto& player : players_) if (player.controller_index == 1) { local_ = player; break; } rpm_reads_ = static_cast<int>(players_.size()) * 13 + 128; bytes_read_ = static_cast<uint64_t>(rpm_reads_) * 8; return !players_.empty(); }
::cs2::HudRadarState Cs2Radar::read_hud() { ::cs2::Cs2GameReader reader{&world_, game_pid_}; return reader.resolve_hud_radar(pid_, true); }
const ::cs2::PlayerData* Cs2Radar::local() const { return local_.pawn_index ? &local_ : nullptr; }
void Cs2Radar::render_radar_console(::cs2::IntelMode mode) { ::cs2::render_radar_console("T0 RPM", players_, local(), hud_, mode); }
Cs2RadarStatus Cs2Radar::status() const { int enemies = 0; for (const auto& player : players_) if (local() && player.team != local_.team) ++enemies; return {attached_, static_cast<int>(players_.size()), enemies, static_cast<int>(players_.size()) - enemies - (local() ? 1 : 0), static_cast<int>(backend_.handle_count()), bytes_read_, rpm_reads_, "RPM Source 2 chunked entity-list + CCSGO_HudRadar", hud_}; }
uint32_t Cs2Radar::pid() const { return pid_; } uint32_t Cs2Radar::game_pid() const { return game_pid_; }
}  // namespace t0_cs2
