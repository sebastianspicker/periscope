// live_radar_loop.cpp — main overlay frame loop for live_radar demo.
#include "demos/live_radar_support.hpp"
#include "demos/radar_shared.hpp"
#include "real/cs2/hijack_reader.hpp"
#include "real/cs2/periscope_radar.hpp"
#include "real/win/api_table.hpp"
#include "real/win/timing.hpp"
#include "real/win/xorstr.hpp"

#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

#if LR_PLATFORM_WINDOWS
#include "real/win/windows_h.hpp"
#endif

int run_live_radar_loop(LiveRadarLoopArgs& a) {
  auto& ladder = *a.ladder;
  auto& donor_ipc = *a.donor_ipc;
  auto& attach = *a.attach;
  auto& reader = *a.reader;
  auto& offsets = *a.offsets;
  auto* renderer = a.renderer;
  auto& hud_settings = *a.hud_settings;
  auto& hud_reader = *a.hud_reader;
  auto& cvars = *a.cvars;
  auto& settings = *a.settings;
  auto& gui = *a.gui;
  const std::uint64_t client_base = a.client_base;
  const std::uint64_t engine_base = a.engine_base;
  const std::uint64_t tier0_base = a.tier0_base;
  const std::size_t client_size = a.client_size;
  const std::size_t engine_size = a.engine_size;
  const std::size_t tier0_size = a.tier0_size;
  std::uint64_t c_hud = a.c_hud;
  bool hud_ok = a.hud_ok;
  const bool ui_holds_cs2_handle = a.ui_holds_cs2_handle;
  BlipFilter filter = a.filter;

  logf("[step 5] Loop...\n");
  logf("  throttle: scatter@20Hz local@45 far@6 | ladder=%s ui_detached=%d\n",
       real::cs2::stack::AttachLadder::stage_name(ladder.stage),
       ladder.ui_detached ? 1 : 0);
  // Apply the GUI filter selector loaded from config (0 all, 1 enemies, 2 team).
  filter = (settings.filter == 1)   ? BlipFilter::EnemyOnly
           : (settings.filter == 2) ? BlipFilter::TeamOnly
                                    : BlipFilter::All;
  float smooth_yaw_deg = 0.f;
  bool have_smooth_yaw = false;
  auto last_layout_refresh = std::chrono::steady_clock::now();
  auto last_cvar_refresh = last_layout_refresh;
  auto last_blue_log = last_layout_refresh;
  auto start_time = std::chrono::high_resolution_clock::now();
  auto blue_window_start = last_layout_refresh;
  std::uint64_t blue_reads_mark = 0;
  std::uint64_t blue_bytes_mark = 0;
  int frame_count = 0;
  int last_entity_count = -1;
  bool first_frame = true;
  bool key_f_was = false, key_r_was = false, key_d_was = false, key_b_was = false;
  bool insert_was = false, lbtn_was = false, key_i_was = false;
  // HudEcho is the default posture: the radar shows only what the official
  // in-game radar could show. The I key cycles auto → pinned hud-echo → pinned
  // full; a pinned mode is never silently overridden by the degrade ladder.
  // `--full` starts pinned to full, `--strict-intel` starts pinned to hud-echo.
  real::cs2::stack::IntelPolicy intel_policy =
      a.full_intel ? real::cs2::stack::IntelPolicy::Full
                   : (a.strict_intel ? real::cs2::stack::IntelPolicy::HudEcho
                                     : real::cs2::stack::IntelPolicy::Auto);
  real::cs2::stack::IntelMode intel_mode = real::cs2::stack::IntelMode::HudEcho;
  real::cs2::stack::BehaviorShaper shaper{};
  real::cs2::stack::DegradeLadder ladder_policy{};
  const real::cs2::stack::ReadRates posture_floor{8, 15, 4};
  real::cs2::stack::ReadThrottle throttle{};
  // Quiet floors (BehaviorShaper is the authority; events raise briefly).
  throttle.target_hz = posture_floor.full_hz;
  throttle.local_hz = posture_floor.local_hz;
  throttle.far_hz = posture_floor.far_hz;
  throttle.arm_full(std::chrono::steady_clock::now());
  throttle.arm_local(std::chrono::steady_clock::now());
  throttle.arm_far(std::chrono::steady_clock::now());
  real::cs2::EntityReadResult entities{};
  real::Result<real::cs2::Cs2PlayerEntity> local{};
  real::cs2::stack::RoundIntel round_intel{};
  ac::Vec3 prev_local_origin{};
  bool have_prev_local = false;
  while (true) {
    if (!renderer->process_messages()) break;

#if LR_PLATFORM_WINDOWS
    // Hotkeys
    const bool key_f = (GetAsyncKeyState('F') & 0x8000) != 0;
    const bool key_r = (GetAsyncKeyState('R') & 0x8000) != 0;
    const bool key_d = (GetAsyncKeyState('D') & 0x8000) != 0;
    const bool key_b = (GetAsyncKeyState('B') & 0x8000) != 0;
    const bool key_insert = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
    const bool key_i = (GetAsyncKeyState('I') & 0x8000) != 0;
    const bool lbtn_down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    if (key_f && !key_f_was) {
      filter = static_cast<BlipFilter>((static_cast<int>(filter) + 1) % 3);
      logf("[input] filter=%s\n", filter_name(filter));
    }
    if (key_d && !key_d_was) {
      auto stl = renderer->overlay_style();
      stl.debug_range_ring = !stl.debug_range_ring;
      (void)renderer->apply_overlay_style(stl);
      logf("[input] debug_range_ring=%d\n", stl.debug_range_ring ? 1 : 0);
    }
    if (key_r && !key_r_was) {
      logf("[input] force layout refresh\n");
      last_layout_refresh = std::chrono::steady_clock::now() -
                            std::chrono::seconds(10);
      last_cvar_refresh = last_layout_refresh;
    }
    if (key_b && !key_b_was) {
      real::cs2::stack::BlueDualConfig bcfg{};
      bcfg.cs2_pid = ladder.cs2_pid;
      bcfg.self_pid = GetCurrentProcessId();
      bcfg.worker_pid = ladder.worker_pid;
      bcfg.donor_pid = ladder.donor_pid;
      bcfg.rpm_reads = g_live_reads - blue_reads_mark;
      bcfg.rpm_bytes = g_live_bytes - blue_bytes_mark;
      bcfg.window_sec = 5.0;
      bcfg.ui_holds_openprocess = ui_holds_cs2_handle;
      bcfg.has_hijack = ladder.hijack_handle;
      bcfg.has_worker = ladder.worker_ipc;
      bcfg.ui_detached = ladder.ui_detached;
      auto sc = real::cs2::stack::score_blue_dual(bcfg);
      logf("[blue-dual] composite=%d detect=%d reason=%s scar_owner=%s scar_pid=%u "
           "handle=%d vol=%d co=%d\n",
           sc.composite, sc.would_detect ? 1 : 0, sc.primary_reason, sc.scar_owner,
           sc.scar_pid, sc.handle_score, sc.volume_score, sc.cooccur_score);
    }
    if (key_insert && !insert_was) {
      gui.toggle_open();
      logf("[input] control_panel=%s\n", gui.is_open() ? "open" : "closed");
    }
    if (key_i && !key_i_was) {
      intel_policy = real::cs2::stack::next_intel_policy(intel_policy);
      logf("[input] intel_policy=%s\n",
           real::cs2::stack::intel_policy_name(intel_policy));
    }
    key_f_was = key_f;
    key_r_was = key_r;
    key_d_was = key_d;
    key_b_was = key_b;
    key_i_was = key_i;
    insert_was = key_insert;

    // Mouse in window-relative pixels (y-down), for GUI hit-testing.
    real::gpu::gui::GuiInput in;
    POINT cursor{};
    GetCursorPos(&cursor);
    if (void* hwnd = renderer->native_handle()) {
      RECT win_rect{};
      if (GetWindowRect(static_cast<HWND>(hwnd), &win_rect)) {
        in.mouse_x = static_cast<float>(cursor.x - win_rect.left);
        in.mouse_y = static_cast<float>(cursor.y - win_rect.top);
      }
    }
    in.mouse_down = lbtn_down;
    in.mouse_clicked = lbtn_down && !lbtn_was;  // rising edge
    in.wheel = 0;
    lbtn_was = lbtn_down;
    gui.set_input(in);
#endif

    const auto now_steady = std::chrono::steady_clock::now();
    // HudRadar refresh: 5s (was 3s) — cuts IPC; map center is stable enough.
    if (std::chrono::duration_cast<std::chrono::seconds>(now_steady - last_cvar_refresh)
            .count() >= 5) {
      last_cvar_refresh = now_steady;
      if (c_hud) {
        hud_ok = hud_reader.update_snapshot(c_hud, client_base, client_size, live_read,
                                            /*stringScanFallback=*/false);
      }
    }
    static auto last_full_cvar = std::chrono::steady_clock::now();
    // Full cvar re-init is expensive over L2 IPC — every 30s.
    if (c_hud && std::chrono::duration_cast<std::chrono::seconds>(now_steady - last_full_cvar)
                         .count() >= 30) {
      last_full_cvar = now_steady;
      cvars.initialize(engine_base, engine_size, tier0_base, tier0_size, client_base,
                       client_size, c_hud, live_read);
      if (cvars.values().resolutionPath > 0 || cvars.values().allValid) {
        hud_settings.hud_scaling = cvars.values().hudScaling;
        hud_settings.cl_hud_radar_scale = cvars.values().clHudRadarScale;
        hud_settings.cl_radar_scale = cvars.values().clRadarScale;
        hud_settings.safezonex = cvars.values().safezoneX;
        hud_settings.safezoney = cvars.values().safezoneY;
        hud_settings.valid = true;
      }
    }
    // Periodic overlay re-layout (resize / resolution / settings).
    if (std::chrono::duration_cast<std::chrono::seconds>(now_steady - last_layout_refresh)
            .count() >= 2) {
      last_layout_refresh = now_steady;
      auto stl = renderer->overlay_style();
      real::cs2::apply_hud_settings_to_style(hud_settings, stl);
      stl.center_nudge_px = 24;
      stl.draw_background = false;
      stl.draw_border = false;
      (void)renderer->apply_overlay_style(stl);
    }

    if (auto r = renderer->begin_frame(); !r) break;

    // GUI frame must wrap all widget calls; overlay px size from the style.
    const auto& gui_style = renderer->overlay_style();
    gui.begin_frame(gui_style.width > 0 ? gui_style.width : 320,
                    gui_style.height > 0 ? gui_style.height : 320);

    // Scattered index walk + priority throttle (all paths via live_read)
    const bool do_full = throttle.full_due(now_steady);
    const bool do_local = throttle.local_due(now_steady);
    const bool do_far = throttle.far_due(now_steady);
    if (do_full && offsets.entity_list != 0) {
      real::cs2::stack::EntityWalkOptions walk{};
      walk.scatter_indices = true;
      walk.jitter_us = true;
      walk.include_dormant = do_far;
      walk.rng = &g_entity_rng;
      entities = real::cs2::stack::read_entity_list_scattered(live_read, offsets, walk);
      throttle.arm_full(now_steady);
      if (do_far) throttle.arm_far(now_steady);
    }
    if ((do_full || do_local) && offsets.local_player != 0) {
      local = real::cs2::stack::read_local_player_fn(live_read, offsets);
      throttle.arm_local(now_steady);
    }

    // Yaw: prefer dwViewAngles, smooth.
    float view_yaw_deg = 0.f;
    bool have_view_yaw = false;
    if (offsets.view_angles != 0) {
      float ang[3]{};
      if (live_read(offsets.view_angles, ang, sizeof(ang))) {
        if (std::isfinite(ang[1]) && std::fabs(ang[1]) < 720.f) {
          view_yaw_deg = ang[1];
          have_view_yaw = true;
        }
      }
    }

    real::gpu::RadarFrame frame{};
    const auto& hud_snap = hud_reader.snapshot();
    const bool use_polar = hud_ok && hud_snap.valid &&
                           hud_snap.maxVisibilitySquared > 1.f &&
                           std::isfinite(hud_snap.mapTextureScale) &&
                           hud_snap.mapTextureScale > 0.f;

    if (local && local->is_alive && entities.read_successful) {
      float yaw_deg = have_view_yaw ? view_yaw_deg : local->eye_angles.y;
      if (!std::isfinite(yaw_deg)) yaw_deg = 0.f;
      if (!have_smooth_yaw) {
        smooth_yaw_deg = yaw_deg;
        have_smooth_yaw = true;
      } else {
        smooth_yaw_deg = smooth_angle_deg(smooth_yaw_deg, yaw_deg, 4.5f);
      }
      yaw_deg = smooth_yaw_deg;
      const float yaw_rad = yaw_deg * (3.14159265f / 180.0f);

      const float cl_zoom = hud_settings.cl_radar_scale > 0.2f
                                ? hud_settings.cl_radar_scale
                                : 0.7f;
      const float fallback_scale = radar_world_scale(cl_zoom);
      const float edge =
          use_polar ? std::sqrt(hud_snap.maxVisibilitySquared) : fallback_scale;
      const float rtts = use_polar ? hud_snap.mapTextureScale : 1.f;

      frame.local_yaw = yaw_rad;
      frame.local_origin = local->origin;
      frame.radar_scale = edge;

      const std::uint8_t local_team = static_cast<std::uint8_t>(local->team);

      auto push_entity_blip = [&](const real::cs2::Cs2PlayerEntity& ent) {
        if (!ent.is_alive && !ent.is_bomb) return;
        if (!ent.is_local_player && !ent.is_bomb) {
          if (ent.dormant) return;
          // HudEcho: hide unspotted enemies — exactly what the official radar
          // shows. In strict mode an unreadable spotted flag counts as hidden,
          // so a failed read cannot leak an unspotted enemy.
          if (intel_mode == real::cs2::stack::IntelMode::HudEcho &&
              ent.team != local_team) {
            if (!ent.spotted_known) {
              if (a.strict_intel) return;
            } else if (!ent.spotted) {
              return;
            }
          }
          if (filter == BlipFilter::EnemyOnly && ent.team == local_team) return;
          if (filter == BlipFilter::TeamOnly && ent.team != local_team) return;
        }

        real::gpu::RadarBlipLayout blip{};
        blip.health = ent.health;
        blip.armor = ent.armor;
        blip.is_local = ent.is_local_player;
        blip.is_alive = ent.is_alive;
        blip.is_visible = ent.spotted;
        blip.is_bomb = ent.is_bomb;
        blip.is_hostage = ent.is_hostage;
        blip.team = static_cast<std::uint8_t>(ent.team);

        if (use_polar) {
          real::cs2::periscope::Vector3 w{ent.origin.x, ent.origin.y, ent.origin.z};
          bool oob = false;
          auto p = real::cs2::periscope::polar_map_player_to_radar(
              hud_snap, w, yaw_rad, rtts, &oob);
          // Map polar axes into overlay blip space (right / up).
          real::cs2::periscope::polar_to_overlay_blip(p.x, p.y, edge, blip.screen_x,
                                                     blip.screen_y);
          (void)oob;
        } else {
          const float lx = local->origin.x, ly = local->origin.y;
          const float dx = ent.origin.x - lx, dy = ent.origin.y - ly;
          const float cf = std::cos(yaw_rad), sf = std::sin(yaw_rad);
          const float forward = dx * cf + dy * sf;
          const float right = dx * sf - dy * cf;
          blip.screen_x = right / edge;
          blip.screen_y = -forward / edge;
        }

        if (ent.is_local_player) {
          blip.angle = -3.14159265f * 0.5f;
          blip.color = 0xFFFFFFFF;
          std::snprintf(blip.label, sizeof(blip.label), "YOU");
        } else if (ent.is_bomb) {
          blip.angle = 0.f;
          blip.color = 0xFFFFFF22;
          std::snprintf(blip.label, sizeof(blip.label), "C4");
        } else {
          blip.angle = (ent.eye_angles.y - yaw_deg) * (3.14159265f / 180.0f);
          if (ent.team == 3) blip.color = 0xFF4488CC;
          else if (ent.team == 2) blip.color = 0xFFCC6644;
          else blip.color = 0xFF888888;
        }
        frame.blips.push_back(blip);
      };

      round_intel = {};
      round_intel.is_round = hud_snap.isRound;
      round_intel.map_texture_scale = hud_snap.mapTextureScale;
      round_intel.radar_scale = hud_snap.radarScale;
      for (const auto& ent : entities.entities) {
        if (ent.is_bomb) continue;
        if (ent.is_alive) {
          if (ent.team == 2) ++round_intel.alive_t;
          else if (ent.team == 3) ++round_intel.alive_ct;
        }
        if (ent.is_spectator || ent.life_state == 2) ++round_intel.spectator_count;
        push_entity_blip(ent);
      }

      if (client_base) {
        real::cs2::Cs2PlayerEntity bomb{};
        if (read_planted_bomb(client_base, bomb)) {
          round_intel.bomb_planted = true;
          round_intel.bomb_site = bomb.bomb_site;
          round_intel.bomb_blow_time = bomb.bomb_blow_time;
          round_intel.bomb_defusing = bomb.bomb_defusing;
          round_intel.bomb_origin = bomb.origin;
          push_entity_blip(bomb);
        }
      }

      // Behavior shaping: reads cluster around gameplay events. Track the
      // local player's movement to fold rotates/retakes into the bursts.
      float local_move = 0.f;
      if (have_prev_local && local) {
        const float dx = local->origin.x - prev_local_origin.x;
        const float dy = local->origin.y - prev_local_origin.y;
        const float dz = local->origin.z - prev_local_origin.z;
        local_move = std::sqrt(dx * dx + dy * dy + dz * dz);
      }
      if (local) {
        prev_local_origin = local->origin;
        have_prev_local = true;
      }
      round_intel.valid = true;
      shaper.note_round(real::cs2::stack::round_events_of(round_intel), local_move,
                        now_steady);
      // Event shaping raises the floor, the degrade ladder caps it, and a
      // pinned policy decides intel. Applying the ladder last is what lets a
      // degrade survive the next frame's shaping.
      const auto posture = real::cs2::stack::apply_posture(
          shaper, ladder_policy, intel_policy, now_steady, posture_floor);
      real::cs2::stack::apply_rates(throttle, posture.rates);
      intel_mode = posture.mode;

      if (first_frame) {
        logf("[frame0] entities=%d polar=%d edge=%.1f yaw=%.1f filter=%s blips=%zu\n",
             entities.entity_count, use_polar ? 1 : 0, edge, yaw_deg,
             filter_name(filter), frame.blips.size());
        logf("[frame0] intel round=%d bomb=%d site=%d defuse=%d blow=%.1f "
             "aliveT/CT=%d/%d spec=%d\n",
             round_intel.is_round ? 1 : 0, round_intel.bomb_planted ? 1 : 0,
             round_intel.bomb_site, round_intel.bomb_defusing ? 1 : 0,
             round_intel.bomb_blow_time, round_intel.alive_t, round_intel.alive_ct,
             round_intel.spectator_count);
        if (use_polar) {
          const float mdx = hud_snap.mapTexturePosition.x - local->origin.x;
          const float mdy = hud_snap.mapTexturePosition.y - local->origin.y;
          logf("[frame0] mapCenter=(%.1f,%.1f) local=(%.1f,%.1f) dXY=(%.1f,%.1f) "
               "rtts=%.5f maxVisSq=%.1f\n",
               hud_snap.mapTexturePosition.x, hud_snap.mapTexturePosition.y,
               local->origin.x, local->origin.y, mdx, mdy, rtts,
               hud_snap.maxVisibilitySquared);
        }
      }
    }

    renderer->maintain_overlay();
    renderer->draw_radar_frame(frame);

    // Control panel layers on top of the radar. radar_panel returns true when
    // any widget activated; only then re-apply the overlay style (cheap).
    bool gui_changed = false;
    if (gui.is_open()) {
      gui_changed = real::gpu::gui::radar_panel(gui, settings);
      if (gui_changed) {
        auto stl = renderer->overlay_style();
        stl.window_alpha = static_cast<std::uint8_t>(settings.overlay_alpha);
        stl.always_on_top = settings.overlay_topmost;
        stl.clickthrough = settings.overlay_clickthrough;
        stl.cl_radar_scale = settings.radar_scale;
        stl.draw_background = settings.draw_bg;
        stl.draw_border = settings.draw_border;
        stl.draw_crosshair = settings.draw_crosshair;
        stl.draw_enemy_arrows = settings.enemy_arrows;
        stl.draw_health_rings = settings.health_rings;
        stl.radar_bg = settings.radar_bg;
        stl.radar_border = settings.radar_border;
        stl.crosshair = settings.crosshair_color;
        (void)renderer->apply_overlay_style(stl);
        hud_settings.cl_radar_scale = settings.radar_scale;
        // Map the GUI filter selector onto the frame filter.
        filter = (settings.filter == 1)   ? BlipFilter::EnemyOnly
                 : (settings.filter == 2) ? BlipFilter::TeamOnly
                                          : BlipFilter::All;
        logf("[gui] style applied alpha=%d topmost=%d click=%d radar_scale=%.2f "
             "filter=%d bg=%d border=%d cross=%d arrows=%d rings=%d\n",
             settings.overlay_alpha, settings.overlay_topmost ? 1 : 0,
             settings.overlay_clickthrough ? 1 : 0, settings.radar_scale,
             settings.filter, settings.draw_bg ? 1 : 0,
             settings.draw_border ? 1 : 0, settings.draw_crosshair ? 1 : 0,
             settings.enemy_arrows ? 1 : 0, settings.health_rings ? 1 : 0);
      }
    }
    gui.end_frame();
    if (auto r = renderer->end_frame(); !r) break;

    if (first_frame) {
      logf("[frame0] OK\n");
      first_frame = false;
    }

    frame_count++;
    auto now = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time)
                  .count();
    if (entities.entity_count != last_entity_count) {
      last_entity_count = entities.entity_count;
      logf("[radar] entities=%d filter=%s polar=%d\n", entities.entity_count,
           filter_name(filter), use_polar ? 1 : 0);
    }
    if (ms >= 5000) {
      float fps = static_cast<float>(frame_count) / (ms / 1000.0f);
      logf("[radar] FPS: %.1f | entities: %d | local: %s | filter=%s | polar=%d\n",
           fps, entities.entity_count,
           local && local->is_alive ? "alive" : "n/a", filter_name(filter),
           use_polar ? 1 : 0);
      if (hud_ok && hud_reader.snapshot().valid) {
        logf("[state] round=%d bomb=%d site=%d defuse=%d blow=%.1f "
             "aliveT/CT=%d/%d spec=%d vis=%.0f\n",
             round_intel.is_round ? 1 : 0, round_intel.bomb_planted ? 1 : 0,
             round_intel.bomb_site, round_intel.bomb_defusing ? 1 : 0,
             round_intel.bomb_blow_time, round_intel.alive_t, round_intel.alive_ct,
             round_intel.spectator_count, hud_reader.snapshot().visibilitySize);
      }
      frame_count = 0;
      start_time = now;
    }

    if (std::chrono::duration_cast<std::chrono::seconds>(now_steady - last_blue_log)
            .count() >= 5) {
      last_blue_log = now_steady;
      const double win =
          std::chrono::duration<double>(now_steady - blue_window_start).count();
      real::cs2::stack::BlueDualConfig bcfg{};
      bcfg.cs2_pid = ladder.cs2_pid;
#if LR_PLATFORM_WINDOWS
      bcfg.self_pid = GetCurrentProcessId();
#endif
      bcfg.worker_pid = ladder.worker_pid;
      bcfg.donor_pid = ladder.donor_pid;
      bcfg.rpm_reads = g_live_reads - blue_reads_mark;
      bcfg.rpm_bytes = g_live_bytes - blue_bytes_mark;
      bcfg.window_sec = win > 0.5 ? win : 5.0;
      bcfg.ui_holds_openprocess = ui_holds_cs2_handle;
      bcfg.has_hijack = ladder.hijack_handle;
      bcfg.has_worker = ladder.worker_ipc;
      bcfg.ui_detached = ladder.ui_detached;
      auto sc = real::cs2::stack::score_blue_dual(bcfg);
      // Silent degrade: a rising composite steps the posture down (slower
      // scans, hud-echo intel) with no visible reaction; three clean windows
      // relax it one rung. The step only chooses rates/mode for `auto` — a
      // pinned policy still wins inside apply_posture.
      real::cs2::stack::ReadRates stepped{};
      ladder_policy.step(sc.composite, shaper.rates_at(now_steady, posture_floor),
                         stepped);
      logf("[blue] ladder=%s ui_detached=%d %s\n",
           real::cs2::stack::AttachLadder::stage_name(ladder.stage),
           ladder.ui_detached ? 1 : 0, ladder.last_scar);
      logf("[blue-dual] composite=%d detect=%d reason=%s scar_owner=%s scar_pid=%u "
           "handle=%d vol=%d co=%d policy=%s intel=%s posture=%d reads~%llu\n",
           sc.composite, sc.would_detect ? 1 : 0, sc.primary_reason, sc.scar_owner,
           sc.scar_pid, sc.handle_score, sc.volume_score, sc.cooccur_score,
           real::cs2::stack::intel_policy_name(intel_policy),
           real::cs2::stack::intel_mode_name(intel_mode), ladder_policy.level,
           (unsigned long long)sc.rpm_reads_window);
      blue_reads_mark = g_live_reads;
      blue_bytes_mark = g_live_bytes;
      blue_window_start = now_steady;
    }

    // Slightly longer sleep when no entities (menu/loading) — lower CPU + IPC.
    if (entities.entity_count <= 0)
      real::win::fuzzed_sleep(22, 12);
    else
      real::win::fuzzed_sleep(16, 10);
  }

  // Persist GUI panel settings to disk.
  {
    const std::string cfg = real::gpu::gui::default_config_path();
    (void)real::gpu::gui::save_radar_config(cfg.c_str(), settings);
    logf("[cleanup] saved gui config: %s\n", cfg.c_str());
  }

  logf("\n[cleanup] forensic exit...\n");
  renderer->shutdown();
  delete renderer;
  g_reader = nullptr;
  g_donor_ipc = nullptr;
  donor_ipc.disconnect();
  reader.detach();
#if LR_PLATFORM_WINDOWS
  real::cs2::hijack::g_Hijack().shutdown();
  real::cs2::stack::forensic_cleanup_ipc();
#endif
  if (attach.handle) real::cs2::detach_from_cs2(attach.handle);
  logf("Done. ladder=%s ui_detached=%d reads=%llu\n",
       real::cs2::stack::AttachLadder::stage_name(ladder.stage),
       ladder.ui_detached ? 1 : 0, (unsigned long long)g_live_reads);
  return 0;
}
