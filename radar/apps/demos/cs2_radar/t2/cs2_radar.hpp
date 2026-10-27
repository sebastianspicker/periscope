#pragma once
#include "cs2/entities.hpp"
#include "cs2/radar_console.hpp"
#include "cs2/radar_math.hpp"
#include "cs2/simulator.hpp"
#include "sim/world.hpp"
#include "t2_red/kernel_radar.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace t2_cs2 {
enum class EscalationTier { T0_Rpm = 0, T1_Syscall = 1, T2_Kernel = 2, T3_Hv = 3, T4_Dma = 4 };
struct EscalationState { EscalationTier active = EscalationTier::T0_Rpm; bool can_escalate_to[5] = {}; std::vector<EscalationTier> degraded_path; };
struct BlueSensorReport {
  bool handle_graph = false; bool cooccurrence = false; bool handle_race = false; bool overlay = false;
  bool module_integrity = false; bool kernel_driver = false; bool device_watch = false; bool callback_tamper = false;
  bool hv_probe = false; bool bridge_intel = false; bool trust_policy = false; bool attestation = false;
  bool dma_device = false; bool iommu_off = false; bool structural_fog = false; bool info_advantage = false;
  double risk = 0.0; int signal_count = 0; std::vector<std::string> reasons;
};
// All techniques available to red at this tier.
struct RedTechniques {
  bool byovd_load = false, custom_driver = false, ioctl_read = false, callback_strip = false;
  bool callback_shadow = false, dkom_hide = false, physmem_map = false, pool_tag_hide = false;
  bool minifilter_strip = false, registry_notify_strip = false, etw_ti_blind = false, scm_service = false;
  int active_count = 0;
};
struct RedCapability { const char* name; const char* description; bool implemented; };
std::vector<RedCapability> list_red_capabilities();
struct TechniqueProfile { const char* name; double concealment; int scar_count; const char* scar_detail; };
struct AdaptiveResult { const char* technique_name; bool applied; bool detected; double concealment; std::string blue_reason; };
struct AdaptiveRunReport {
  int total_techniques; int attempted; int survived; int detected_count;
  double best_surviving_concealment; std::vector<AdaptiveResult> results;
};
struct Cs2RadarStatus { bool attached; int entity_count; int enemies; int teammates; int handle_count; uint64_t bytes_read; int read_ops; std::string detail; ::cs2::HudRadarState hud_state; };
class Cs2Radar { public:
  explicit Cs2Radar(sim::World& w) : world_(w), transport_(w, "cs2_radar_t2.exe") {}
  bool attach_kernel(); bool bring_up_kernel(); bool scan_entities(); void render_radar_console(::cs2::IntelMode mode = ::cs2::IntelMode::HudEcho);
   EscalationState escalate_down(); BlueSensorReport blue_multi_sensor_scan();
  Cs2RadarStatus status() const; uint32_t pid() const { return transport_.ui_pid(); } uint32_t game_pid() const { return world_.game_pid(); }
   const std::vector<::cs2::PlayerData>& players() const { return players_; } const ::cs2::PlayerData* local() const; ::cs2::HudRadarState read_hud();
   void apply_technique(const char* name); void apply_all_techniques(); void clear_techniques();
   const RedTechniques& red_techniques() const { return techniques_; }
   TechniqueProfile get_technique_profile(const char* name);
   AdaptiveResult try_technique(const char* name);
   AdaptiveRunReport adaptive_red_loop();
  private: sim::World& world_; t2_red::KernelRadar transport_; bool attached_ = false; std::vector<::cs2::PlayerData> players_; ::cs2::PlayerData local_{}; ::cs2::HudRadarState hud_{}; int reads_ = 0; uint64_t bytes_ = 0; RedTechniques techniques_{}; };
}  // namespace t2_cs2
