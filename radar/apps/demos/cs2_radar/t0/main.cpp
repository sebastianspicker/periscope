// cs2_radar/t0/main.cpp — T0 Demo Live Radar Overlay (Periscope startup flow).
//
// Startup flow:
//   1. Resolve API table (PEB+EAT)
//   2. Find CS2 process (CreateToolhelp32Snapshot)
//   3. Setup hijack reader (donor handle duplication)
//   4. Enumerate CS2 modules (EnumProcessModulesEx)
//   5. Scan patterns -> build offsets
//   6. Init stealth overlay (randomized names, WDA)
//   7. Frame loop: collect -> hud -> cvar -> render -> heal -> jittered sleep
//
// Educational: every step documents the OS artifact it creates.
// Build: cmake -S . -B build -DLR_ENABLE_REAL_RPM=ON -DLR_ENABLE_REAL_SYSCALL=ON
// Run:   ./build/cs2_radar_t0 (requires cs2.exe running)

#include "cs2_radar.hpp"
#include "cs2/simulator.hpp"
#include "radar_pipeline.hpp"  // Integrates ALL 15+ ac_sim features

#if LR_PLATFORM_WINDOWS
#include "real/win/api_table.hpp"
#include "real/win/xorstr.hpp"
#include "real/cs2/process_finder.hpp"
#include "real/cs2/hijack_reader.hpp"
#include "real/cs2/periscope_scanner.hpp"
#include "real/cs2/periscope_entity.hpp"
#include "real/cs2/periscope_hud.hpp"
#include "real/cs2/periscope_radar.hpp"
#include "real/gpu/periscope_overlay.hpp"
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <thread>
#include <vector>

#if LR_PLATFORM_WINDOWS
static bool hijack_read_fn(uint64_t addr, void* buf, size_t size) {
  return real::cs2::hijack::g_Hijack().read(addr, buf, size);
}
#endif

static int run_sim_adaptive(bool full_intel = false) {
  std::printf("\n=== T0 CS2 Radar SIMULATION (arena, no live CS2) ===\n");
  auto arena = ::cs2::make_cs2_arena();
  std::printf("T0 red capability catalog:\n");
  for (const auto& c : t0_cs2::list_red_capabilities())
    std::printf("  %s: %s\n", c.name, c.description);
  t0_cs2::Cs2Radar radar(arena.world);
  if (!radar.attach_rpm() || !radar.scan_entities()) {
    std::fprintf(stderr, "FAIL: sim attach/scan failed\n");
    return 1;
  }
  radar.render_radar_console(full_intel ? ::cs2::IntelMode::Full
                                        : ::cs2::IntelMode::HudEcho);
  const auto report = radar.adaptive_red_loop();
  std::printf("T0 adaptive verdict: survived=%d detected=%d\n",
              report.survived, report.detected_count);
  auto blue = radar.blue_multi_sensor_scan();
  std::printf("sensors: risk=%.2f signals=%d\n", blue.risk, blue.signal_count);
  for (const auto& reason : blue.reasons) std::printf("  - %s\n", reason.c_str());
  auto st = radar.status();
  std::printf("status: attached=%d entities=%d enemies=%d\n",
              st.attached ? 1 : 0, st.entity_count, st.enemies);
  return 0;
}

int main(int argc, char** argv) {
  // `--full` pins the all-entity intel mode; the default stays hud-echo
  // (HUD-radar-equivalent intel only).
  const bool full_intel = ::cs2::has_flag(argc, argv, "--full");
  std::printf("=== T0 CS2 Radar Demo (Periscope Startup Flow) ===\n\n");

#if LR_PLATFORM_WINDOWS
  // ── 1. Resolve API table ──────────────────────────────────────
  std::printf("[1/7] Resolving API table (PEB+EAT)...\n");
  auto& api = real::win::g_Api();
  if (!api.resolved) {
    std::fprintf(stderr, "FAIL: API table resolution failed\n");
    return 1;
  }
  std::printf("  OK: %d functions resolved\n", api.resolved);

  // ── 2. Find CS2 process ──────────────────────────────────────
  std::printf("[2/7] Finding CS2 process...\n");
  auto cs2 = real::cs2::find_cs2();
  if (!cs2.has_value()) {
    std::printf("  CS2 not found — falling back to SIMULATION path\n");
    return run_sim_adaptive(full_intel);
  }
  std::printf("  OK: cs2.exe PID=%u\n", cs2->pid);

  // ── 3. Setup hijack reader ───────────────────────────────────
  std::printf("[3/7] Setting up hijack reader (donor handle)...\n");
  if (!real::cs2::hijack::g_Hijack().setup(cs2->pid)) {
    std::fprintf(stderr, "WARN: hijack reader setup failed, trying direct RPM\n");
  } else {
    std::printf("  OK: donor PID=%u\n",
                real::cs2::hijack::g_Hijack().donor_pid());
  }

  // ── 4. Enumerate CS2 modules ─────────────────────────────────
  std::printf("[4/7] Enumerating CS2 modules...\n");
  auto cs2_limited = real::cs2::open_cs2_limited(cs2->pid);
  std::vector<real::cs2::Cs2ModuleInfo> modules;
  if (cs2_limited.has_value()) {
    modules = real::cs2::enumerate_modules(cs2->pid, cs2_limited->hProc);
  }
  if (modules.empty()) {
    std::fprintf(stderr, "WARN: no modules enumerated\n");
  } else {
    std::printf("  OK: %zu modules found\n", modules.size());
    for (const auto& m : modules) {
      std::printf("    %s base=0x%llx size=%zu\n",
                  m.name.c_str(),
                  (unsigned long long)m.base, m.size);
    }
  }

  // ── 5. Scan patterns -> build offsets ────────────────────────
  std::printf("[5/7] Scanning patterns, building offsets...\n");
  uint64_t client_base = 0;
  size_t client_size = 0;
  uint64_t engine2_base = 0;
  size_t engine2_size = 0;
  uint64_t tier0_base = 0;
  size_t tier0_size = 0;

  for (const auto& m : modules) {
    if (m.name.find("client") != std::string::npos) {
      client_base = m.base; client_size = m.size;
    } else if (m.name.find("engine2") != std::string::npos) {
      engine2_base = m.base; engine2_size = m.size;
    } else if (m.name.find("tier0") != std::string::npos) {
      tier0_base = m.base; tier0_size = m.size;
    }
  }

  bool read_fn_ready = real::cs2::hijack::g_Hijack().is_ready();

  // Define read function pointer
  auto read_fn = read_fn_ready ? hijack_read_fn : nullptr;

  real::cs2::periscope::PeriscopeOffsets offsets;
  if (client_base && read_fn_ready) {
    auto results = real::cs2::periscope::scan_all_patterns(
        client_base, client_size, hijack_read_fn);
    std::printf("  Patterns scanned: %zu\n", results.size());
    for (const auto& r : results) {
      std::printf("    %s: %s (0x%llx)\n",
                  r.name.c_str(), r.found ? "FOUND" : "MISS",
                  (unsigned long long)r.address);
    }
  } else {
    // Use RVA fallbacks
    offsets.client_dll = client_base;
    offsets.engine2_dll = engine2_base;
    offsets.tier0_dll = tier0_base;
    offsets.entity_list = client_base + offsets.kDwEntityList;
    offsets.local_pawn = client_base + offsets.kDwLocalPlayerPawn;
    offsets.local_controller = client_base + offsets.kDwLocalPlayerController;
    offsets.c_hud = client_base + 0x6D4A8C0; // Fallback CHud offset
    std::printf("  Using RVA fallbacks (no pattern scanning)\n");
  }
  offsets.valid = offsets.is_complete();
  std::printf("  Offsets valid: %d\n", offsets.valid);

  // ── 6. Init stealth overlay ──────────────────────────────────
  std::printf("[6/7] Initializing stealth overlay...\n");
  real::gpu::periscope::StealthOverlay overlay;
  real::gpu::periscope::OverlayConfig overlay_cfg;
  overlay_cfg.width = 400;
  overlay_cfg.height = 400;
  overlay_cfg.alpha = 200;
  overlay_cfg.topmost = true;
  overlay_cfg.clickthrough = true;

  // Randomized class/title from build seeds
  overlay_cfg.className = real::gpu::periscope::generate_random_name(
      build::kClassSeed ^ 0xA5A5C3C3, "OvrClass");
  overlay_cfg.windowName = real::gpu::periscope::generate_random_name(
      build::kApiSalt, "WinApp");

  if (!overlay.initialize(overlay_cfg)) {
    std::fprintf(stderr, "WARN: overlay init failed (non-fatal for console demo)\n");
  } else {
    std::printf("  OK: overlay HWND=0x%llx\n",
                (unsigned long long)overlay.handle());
    // Initial WDA application
    overlay.ensure_capture_exclusion();
    std::printf("  WDA_EXCLUDEFROMCAPTURE applied\n");
  }

  // ── 7. Frame loop ────────────────────────────────────────────
  std::printf("[7/7] Entering frame loop...\n\n");
  std::printf("=== Radar Active (press ESC on overlay or Ctrl+C to quit) ===\n");

  // ── Initialize the integrated FramePipeline (wires ALL ac_sim features) ─
#if LR_HAS_REAL_PLATFORM
  radar::real_adapter::RealFramePipeline pipeline;
#else
  radar::FramePipeline pipeline;
#endif
  pipeline.initialize(cs2->pid, read_fn_ready);
  std::printf("  FramePipeline initialized: temporal/disguise/behavioral/health/gate/etl active\n\n");

  int frame_count = 0;
  auto start_time = std::chrono::high_resolution_clock::now();

  // Periscope readers
  real::cs2::periscope::EntityCollector entity_collector;
  real::cs2::periscope::HudRadarReader hud_reader;
  real::cs2::periscope::CvarManager cvar_manager;
  real::cs2::periscope::YawState yaw_state;
  real::cs2::periscope::OriginState origin_state;

  while (true) {
    // Process overlay messages (quit on ESC)
    if (overlay.is_initialized()) {
      if (!overlay.process_messages()) break;
    }

    // ── Pipeline pre-frame: temporal, disguise, self-verify, API integrity, DllWatch ──
    pipeline.run_frame();

    // ── Collect entities ──────────────────────────────────────
    if (read_fn_ready && offsets.entity_list) {
      entity_collector.collect(offsets.entity_list, hijack_read_fn);
    }

    // ── Read HUD radar snapshot ───────────────────────────────
    if (read_fn_ready && offsets.c_hud && client_base) {
      hud_reader.update_snapshot(offsets.c_hud, client_base,
                                 client_size, hijack_read_fn);
    }

    // ── Read CVars ────────────────────────────────────────────
    if (read_fn_ready && engine2_base && tier0_base && offsets.c_hud) {
      cvar_manager.initialize(engine2_base, engine2_size,
                               tier0_base, tier0_size,
                               client_base, client_size,
                               offsets.c_hud, hijack_read_fn);
    }

    // ── Update yaw from local player ─────────────────────────
    if (entity_collector.local().valid) {
      yaw_state.update(entity_collector.local().yaw, true);
    }

    // ── Reapply WDA each frame ───────────────────────────────
    if (overlay.is_initialized()) {
      overlay.ensure_capture_exclusion();
    }

    // ── Frame counter ─────────────────────────────────────────
    frame_count++;
    auto now = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - start_time).count();
    if (ms >= 5000) {
      float fps = static_cast<float>(frame_count) / (ms / 1000.0f);
      auto players = entity_collector.players();
      size_t alive = 0;
      for (const auto& p : players) {
        if (p.valid && p.alive) ++alive;
      }
      std::printf("\r[radar] FPS: %.1f | players: %zu/%zu | yaw: %.1f | health: %d | gate: %d/7   ",
                  fps, alive, players.size(),
                  yaw_state.get(),
                  static_cast<int>(pipeline.health().current_level()),
                  pipeline.gate().met_count());
      fflush(stdout);
      frame_count = 0;
      start_time = now;
    }

    // ── Jittered sleep (16ms base, but TemporalEngine determines actual jitter) ──
    auto delayed_us = pipeline.run_frame() ? 0 : 16000; // pipeline handles timing
    int sleep_ms = 16;
    if (delayed_us > 0 && delayed_us < 16000) {
      sleep_ms = delayed_us / 1000;
    }
    if (sleep_ms < 4) sleep_ms = 4;
    std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
  }

  // ── Cleanup ──────────────────────────────────────────────────
  std::printf("\n\n[cleanup] Shutting down...\n");
  pipeline.shutdown();  // forensic cleanup, ETL flush, all subsystems
  overlay.shutdown();
  real::cs2::hijack::g_Hijack().shutdown();
  if (cs2_limited.has_value()) {
    api.CloseHandle(reinterpret_cast<HANDLE>(cs2_limited->hProc));
  }
  std::printf("[cleanup] Complete.\n");

#else
  // Non-Windows: run sim-based demo
  std::printf("Non-Windows platform: running sim-based demo\n");
  return run_sim_adaptive(full_intel);
#endif

  return 0;
}
