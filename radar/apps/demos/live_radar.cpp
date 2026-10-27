/// live_radar.cpp — Full live CS2 radar overlay prototype.
/// Educational anti-cheat lab: T0 external RPM + D3D11 overlay on in-game minimap.
///
/// Build: cmake -S . -B build -DLR_ENABLE_REAL_RPM=ON
/// Run:   ./build/Release/live_radar.exe  (requires cs2.exe running)
///        optional: donor_worker.exe first so UI never OpenProcess(CS2)
/// Keys:  END=quit  F=filter  R=refresh layout  D=debug ring  B=blue score
///
/// Features: HudRadar polar, ICVar walk, bomb/spectator intel, read throttle,
/// shuffled entity batch, attach ladder + SCAR stages, streamproof WDA,
/// blue dual scoring, offset auto-update.

#include "demos/live_radar_support.hpp"
#include "real/cs2/hijack_reader.hpp"
#include "real/cs2/hud_layout.hpp"
#include "real/cs2/live_radar_stack.hpp"
#include "real/cs2/offsets_snapshot.hpp"
#include "real/cs2/periscope_hud.hpp"
#include "real/cs2/periscope_scanner.hpp"
#include "real/gpu/render_pipeline.hpp"
#include "real/gpu/gui.hpp"
#include "real/win/api_table.hpp"
#include "real/win/timing.hpp"
#include "real/win/xorstr.hpp"
#include "ac/types.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#if LR_PLATFORM_WINDOWS
#include "real/win/windows_h.hpp"
#endif

#if defined(_WIN32)
#pragma comment(linker, "/SUBSYSTEM:CONSOLE")
#endif

int main(int argc, char** argv) {
  // `--full` pins the all-entity intel mode. `--strict-intel` pins hud-echo and
  // treats an unreadable spotted flag as hidden. Default is auto: hud-echo with
  // the degrade ladder free to tighten it, never to relax past hud-echo.
  bool full_intel = false;
  bool strict_intel = false;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--full") == 0) full_intel = true;
    if (std::strcmp(argv[i], "--strict-intel") == 0) strict_intel = true;
  }
  setvbuf(stdout, nullptr, _IONBF, 0);
  setvbuf(stderr, nullptr, _IONBF, 0);

  logf("=== Live CS2 Radar Overlay (L2-pure stack) ===\n");
  logf("Keys: END=quit  F=filter  R=refresh  D=debug  I=intel-policy  B=blue score\n");
  logf("Intel policy: %s (default auto → hud-echo; --full pins all-entity)\n\n",
       real::cs2::stack::intel_policy_name(
           full_intel ? real::cs2::stack::IntelPolicy::Full
                      : (strict_intel ? real::cs2::stack::IntelPolicy::HudEcho
                                      : real::cs2::stack::IntelPolicy::Auto)));

#if LR_PLATFORM_WINDOWS
  real::cs2::stack::apply_process_disguise("ui");
  logf("[step 0] Resolving API table (PEB+EAT)...\n");
  auto& api = real::win::g_Api();
  if (!api.resolved) {
    std::fprintf(stderr, "FAIL: API table resolution failed\n");
    return 1;
  }
  logf("  OK\n\n");
#endif

  real::cs2::stack::AttachLadder ladder{};
  real::cs2::stack::DonorIpcClient donor_ipc;
  g_donor_ipc = &donor_ipc;
  real::cs2::AttachResult attach{};
  bool ui_holds_cs2_handle = false;

  logf("[step 1] Attach ladder (L2 pure preferred)...\n");

#if LR_PLATFORM_WINDOWS
  auto try_l2 = [&](const char* tag) -> bool {
    if (!donor_ipc.connect()) return false;
    if (!donor_ipc.wait_modules(2500)) {
      logf("  %s connected but modules not ready\n", tag);
      return false;
    }
    ladder.cs2_pid = donor_ipc.cs2_pid();
    ladder.worker_pid = donor_ipc.worker_pid();
    ladder.set_stage(real::cs2::stack::AttachStage::DonorWorkerIpc);
    g_prefer_worker = true;
    g_prefer_hijack = false;
    if (attach.handle) {
      real::cs2::detach_from_cs2(attach.handle);
      attach.handle = 0;
      attach.attached = false;
    }
    ui_holds_cs2_handle = false;
    logf("  OK %s worker=%u cs2=%u UI_DETACHED=1\n", tag, ladder.worker_pid,
         ladder.cs2_pid);
    logf("  %s\n", ladder.last_scar);
    return true;
  };

  logf("  L2 try existing donor_worker...\n");
  if (!try_l2("L2")) {
    logf("  miss L2 — try spawn then L1/L3\n");
    logf("  L2b spawn donor_worker (disguised)...\n");
    const auto wpid = real::cs2::stack::spawn_donor_worker();
    if (wpid) {
      Sleep(500);
      if (try_l2("L2b")) {
      }
    }
  }

  if (ladder.stage != real::cs2::stack::AttachStage::DonorWorkerIpc) {
    attach = real::cs2::attach_to_cs2();
    if (!attach.attached) {
      logf("  attach miss: %s\n", attach.error_msg.c_str());
      return run_simulation_fallback(attach.error_msg.c_str());
    }
    ui_holds_cs2_handle = true;
    ladder.cs2_pid = attach.pid;
    logf("  %s\n", attach.describe().c_str());

    logf("  L1 try hijack...\n");
    if (real::cs2::hijack::g_Hijack().setup(
            attach.pid,
            reinterpret_cast<void*>(static_cast<uintptr_t>(attach.handle)))) {
      ladder.donor_pid = real::cs2::hijack::g_Hijack().donor_pid();
      ladder.set_stage(real::cs2::stack::AttachStage::HijackVerified);
      g_prefer_hijack = true;
      logf("  OK L1 donor=%u\n", ladder.donor_pid);
      logf("  %s\n", ladder.last_scar);
    } else {
      ladder.set_stage(real::cs2::stack::AttachStage::DirectOpenProcess);
      logf("  OK L3 direct OpenProcess\n");
      logf("  %s\n", ladder.last_scar);
    }
  }
#else
  attach = real::cs2::attach_to_cs2();
  if (!attach.attached) {
    logf("  attach miss: %s\n", attach.error_msg.c_str());
    return run_simulation_fallback(attach.error_msg.c_str());
  }
  ladder.cs2_pid = attach.pid;
  ladder.set_stage(real::cs2::stack::AttachStage::DirectOpenProcess);
  ui_holds_cs2_handle = true;
#endif
  logf("\n");

  std::uint64_t client_base = 0, engine_base = 0, tier0_base = 0;
  std::size_t client_size = 0, engine_size = 0, tier0_size = 0;
  std::uint64_t main_base = attach.base_address;
  std::size_t main_size = attach.image_size;

  if (ladder.stage == real::cs2::stack::AttachStage::DonorWorkerIpc) {
    auto mods = donor_ipc.modules();
    client_base = mods.client_base;
    client_size = mods.client_size;
    engine_base = mods.engine2_base;
    engine_size = mods.engine2_size;
    tier0_base = mods.tier0_base;
    tier0_size = mods.tier0_size;
    main_base = mods.cs2_base ? mods.cs2_base : mods.client_base;
    main_size = mods.cs2_image_size ? mods.cs2_image_size : mods.client_size;
    logf("[step 1b] L2 modules from worker (no UI OpenProcess)\n");
  } else if (attach.attached) {
    real::cs2::find_client_module(attach.pid, attach.handle, client_base, client_size);
    real::cs2::find_module_by_basename(attach.pid, attach.handle, "engine2.dll",
                                       engine_base, engine_size);
    real::cs2::find_module_by_basename(attach.pid, attach.handle, "tier0.dll",
                                       tier0_base, tier0_size);
  }

  logf("[step 2] Offset auto-update...\n");
  real::cs2::Cs2Offsets offsets{};
  {
    const std::uint64_t ph =
        (ladder.stage == real::cs2::stack::AttachStage::DonorWorkerIpc) ? 0
                                                                        : attach.handle;
    const std::uint64_t base_for_resolve =
        (ph == 0) ? client_base : main_base;
    auto ur = real::cs2::stack::auto_update_offsets(
        ladder.cs2_pid, ph, base_for_resolve, main_size, offsets, ph != 0);
    if (client_base && offsets.entity_list < 0x100000)
      offsets = real::cs2::stack::offsets_from_client_base(client_base);
    else if (client_base && offsets.entity_list < client_base)
      offsets = real::cs2::offsets_from_snapshot(client_base);
    logf("  snapshot=%d complete=%d wrote=%d detail=%s\n", ur.snapshot_loaded ? 1 : 0,
         offsets.is_complete() ? 1 : 0, ur.wrote_snapshot ? 1 : 0, ur.detail.c_str());
  }

  logf("[step 3] Memory backend...\n");
  real::cs2::Cs2MemoryReader reader;
  if (ladder.stage != real::cs2::stack::AttachStage::DonorWorkerIpc &&
      attach.attached && attach.pid) {
    auto st = reader.attach(ac::Tier::T0_UsermodeRpm, attach.pid);
    if (st != ac::Status::Ok &&
        ladder.stage == real::cs2::stack::AttachStage::DirectOpenProcess) {
      std::fprintf(stderr, "FAIL: reader attach\n");
      real::cs2::detach_from_cs2(attach.handle);
      return 1;
    }
    if (st == ac::Status::Ok) g_reader = &reader;
  }
  logf("  backend=%s ladder=%s hijack=%d worker=%d ui_detached=%d\n\n",
       g_prefer_worker ? "donor_ipc"
                       : (g_reader ? reader.active_backend_name() : "none"),
       real::cs2::stack::AttachLadder::stage_name(ladder.stage),
       g_prefer_hijack ? 1 : 0, g_prefer_worker ? 1 : 0,
       ladder.ui_detached ? 1 : 0);

  logf("[step 3b] modules client=0x%llx engine2=0x%llx tier0=0x%llx\n",
       (unsigned long long)client_base, (unsigned long long)engine_base,
       (unsigned long long)tier0_base);

  logf("[step 3c] Resolving CHud + HudRadar + cvars...\n");
  std::uint64_t c_hud = resolve_c_hud(client_base, client_size);
  logf("  c_hud=%s 0x%llx\n", c_hud ? "OK" : "MISS", (unsigned long long)c_hud);

  real::cs2::periscope::HudRadarReader hud_reader;
  real::cs2::periscope::CvarManager cvars;
  bool hud_ok = false;
  if (c_hud) {
    logf("  updating HudRadar snapshot (no full-module string scan)...\n");
    hud_ok = hud_reader.update_snapshot(c_hud, client_base, client_size, live_read,
                                        /*stringScanFallback=*/false);
    logf("  HudRadar valid=%d\n", hud_ok ? 1 : 0);
    logf("  initializing cvars (ICVar CreateInterface + HUD)...\n");
    cvars.initialize(engine_base, engine_size, tier0_base, tier0_size, client_base,
                     client_size, c_hud, live_read);
    {
      const int path = cvars.values().resolutionPath;
      const char* path_name =
          path == 1 ? "warm_cache" :
          path == 2 ? "icvar_list_walk" :
          path == 3 ? "hud_mem" :
          path == 4 ? "xref_pe_scan" : "defaults";
      logf("  cvars path=%d (%s) allValid=%d\n", path, path_name,
           cvars.values().allValid ? 1 : 0);
    }
    logf("  cvars: hud_scaling=%.3f cl_hud_radar=%.3f cl_radar=%.3f safe=(%.2f,%.2f)\n",
         cvars.values().hudScaling, cvars.values().clHudRadarScale,
         cvars.values().clRadarScale, cvars.values().safezoneX,
         cvars.values().safezoneY);
    if (hud_ok) {
      const auto& s = hud_reader.snapshot();
      logf("  HudRadar: vis=%.1f/%.1f mapTexScale=%.6f maxVisSq=%.1f radarScale=%.3f round=%d\n",
           s.visibilitySize, s.visibilitySizeMax, s.mapTextureScale,
           s.maxVisibilitySquared, s.radarScale, s.isRound ? 1 : 0);
    }
  }

  real::cs2::RadarHudSettings hud_settings{};
  if (cvars.values().allValid || cvars.values().resolutionPath > 0) {
    hud_settings.hud_scaling = cvars.values().hudScaling;
    hud_settings.cl_hud_radar_scale = cvars.values().clHudRadarScale;
    hud_settings.cl_radar_scale = cvars.values().clRadarScale;
    hud_settings.safezonex = cvars.values().safezoneX;
    hud_settings.safezoney = cvars.values().safezoneY;
    hud_settings.valid = true;
    hud_settings.resolved_count = 5;
  } else {
    const char* force_scan = std::getenv("LR_FORCE_CVAR_SCAN");
    if (force_scan && force_scan[0] == '1' && client_base) {
      logf("  LR_FORCE_CVAR_SCAN: PE string scan...\n");
      hud_settings = real::cs2::read_radar_hud_settings(reader, client_base, client_size);
    } else {
      logf("  using default HUD scales (set LR_FORCE_CVAR_SCAN=1 for PE scan)\n");
      hud_settings.hud_scaling = 1.f;
      hud_settings.cl_hud_radar_scale = 1.f;
      hud_settings.cl_radar_scale = 0.7f;
      hud_settings.safezonex = 1.f;
      hud_settings.safezoney = 1.f;
    }
  }

  logf("[step 4] Overlay...\n");
  real::gpu::RenderPipeline* renderer = nullptr;
  {
    real::gpu::OverlayStyle style{};
    style.anchor = real::gpu::OverlayStyle::Anchor::InGameRadar;
    style.always_on_top = true;
    style.clickthrough = true;
    style.window_alpha = 255;
    style.draw_background = false;
    style.draw_border = false;
    style.draw_crosshair = false;
    style.draw_enemy_arrows = true;
    style.draw_health_rings = true;
    style.debug_range_ring = false;
    style.center_nudge_px = 24;
    style.radar_bg = 0x00000000;
    real::cs2::apply_hud_settings_to_style(hud_settings, style);
    style.draw_background = false;
    style.draw_border = false;
    auto created = real::gpu::create_render_pipeline(style, "Radar");
    if (created) {
      renderer = *created;
      (void)renderer->apply_overlay_style(style);
    }
  }
  if (!renderer || !renderer->is_initialized()) {
    std::fprintf(stderr, "FAIL: overlay\n");
    reader.detach();
    if (attach.handle) real::cs2::detach_from_cs2(attach.handle);
    return 1;
  }
  {
    auto lay = real::cs2::compute_radar_screen_rect(
        real::gpu::find_cs2_game_window(), hud_settings, renderer->overlay_style());
    if (lay.valid) {
      logf("  SNAP %dx%d @(%d,%d) client=%dx%d nudge=%d\n", lay.size, lay.size,
           lay.x, lay.y, lay.client_w, lay.client_h, lay.used_nudge);
    }
  }
  {
    void* hwnd = renderer->native_handle();
    auto audit =
        real::cs2::stack::apply_and_audit_streamproof(hwnd, /*only_if_streaming=*/false);
    logf("  streamproof: hwnd=%p applied=%d affinity=0x%x %s\n\n", hwnd,
         audit.wda_exclude_applied ? 1 : 0, audit.affinity_value, audit.detail);
  }

  real::gpu::gui::GuiSystem gui;
  real::gpu::gui::RadarPanelSettings settings{};
  {
    gui.set_renderer(renderer);
    gui.set_open(false);
    const std::string cfg = real::gpu::gui::default_config_path();
    const bool cfg_loaded = real::gpu::gui::load_radar_config(cfg.c_str(), settings);
    auto stl = renderer->overlay_style();
    stl.window_alpha = static_cast<std::uint8_t>(settings.overlay_alpha);
    stl.always_on_top = settings.overlay_topmost;
    stl.clickthrough = settings.overlay_clickthrough;
    stl.cl_radar_scale = settings.radar_scale;
    hud_settings.cl_radar_scale = settings.radar_scale;
    (void)renderer->apply_overlay_style(stl);
    logf("  gui: config=%s panel=hidden alpha=%d topmost=%d click=%d scale=%.2f\n",
         cfg_loaded ? "loaded" : "defaults", settings.overlay_alpha,
         settings.overlay_topmost ? 1 : 0, settings.overlay_clickthrough ? 1 : 0,
         settings.radar_scale);
  }

  LiveRadarLoopArgs loop{};
  loop.ladder = &ladder;
  loop.donor_ipc = &donor_ipc;
  loop.attach = &attach;
  loop.reader = &reader;
  loop.offsets = &offsets;
  loop.renderer = renderer;
  loop.hud_settings = &hud_settings;
  loop.hud_reader = &hud_reader;
  loop.cvars = &cvars;
  loop.client_base = client_base;
  loop.engine_base = engine_base;
  loop.tier0_base = tier0_base;
  loop.client_size = client_size;
  loop.engine_size = engine_size;
  loop.tier0_size = tier0_size;
  loop.c_hud = c_hud;
  loop.hud_ok = hud_ok;
  loop.ui_holds_cs2_handle = ui_holds_cs2_handle;
  loop.full_intel = full_intel;
  loop.strict_intel = strict_intel;
  loop.settings = &settings;
  loop.gui = &gui;
  return run_live_radar_loop(loop);
}
