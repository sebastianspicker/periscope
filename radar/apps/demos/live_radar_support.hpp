// live_radar_support.hpp — helpers/globals shared by live_radar demo TUs.
#pragma once

#include "real/real_fwd.hpp"
#include "real/gpu/render_pipeline.hpp"
#include "real/gpu/gui.hpp"
#include "real/cs2/live_radar_stack.hpp"
#include "real/cs2/entities.hpp"
#include "real/cs2/hud_layout.hpp"
#include "real/cs2/periscope_hud.hpp"
#include "real/cs2/process.hpp"
#include "ac/types.hpp"

#include <cstdint>
#include <cstddef>

enum class BlipFilter { All = 0, EnemyOnly = 1, TeamOnly = 2 };

void logf(const char* fmt, ...);

extern real::cs2::Cs2MemoryReader* g_reader;
extern real::cs2::stack::DonorIpcClient* g_donor_ipc;
extern bool g_prefer_hijack;
extern bool g_prefer_worker;
extern std::uint64_t g_live_reads;
extern std::uint64_t g_live_bytes;
extern std::uint64_t g_entity_rng;

bool live_read(std::uint64_t addr, void* buf, std::size_t size);
float smooth_angle_deg(float current, float target, float max_step_deg);
std::uint64_t resolve_c_hud(std::uint64_t client_base, std::size_t client_size);
bool read_planted_bomb(std::uint64_t client_base, real::cs2::Cs2PlayerEntity& out);
const char* filter_name(BlipFilter f);
int run_simulation_fallback(const char* reason);

struct LiveRadarLoopArgs {
  real::cs2::stack::AttachLadder* ladder = nullptr;
  real::cs2::stack::DonorIpcClient* donor_ipc = nullptr;
  real::cs2::AttachResult* attach = nullptr;
  real::cs2::Cs2MemoryReader* reader = nullptr;
  real::cs2::Cs2Offsets* offsets = nullptr;
  real::gpu::RenderPipeline* renderer = nullptr;
  real::cs2::RadarHudSettings* hud_settings = nullptr;
  real::cs2::periscope::HudRadarReader* hud_reader = nullptr;
  real::cs2::periscope::CvarManager* cvars = nullptr;
  std::uint64_t client_base = 0;
  std::uint64_t engine_base = 0;
  std::uint64_t tier0_base = 0;
  std::size_t client_size = 0;
  std::size_t engine_size = 0;
  std::size_t tier0_size = 0;
  std::uint64_t c_hud = 0;
  bool hud_ok = false;
  bool ui_holds_cs2_handle = false;
  bool full_intel = false;  // --full: pinned all-entity intel mode
  bool strict_intel = false;  // --strict-intel: unreadable spotted ⇒ hidden
  BlipFilter filter = BlipFilter::All;
  real::gpu::gui::RadarPanelSettings* settings = nullptr;
  real::gpu::gui::GuiSystem* gui = nullptr;
};

int run_live_radar_loop(LiveRadarLoopArgs& a);
