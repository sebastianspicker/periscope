// Split from live_radar_stack.cpp — see MONOLITH_REFACTOR_LEDGER.
#include "real/cs2/live_radar_stack.hpp"
#include "real/cs2/live_radar_stack_internal.hpp"
#include "real/cs2/offsets_snapshot.hpp"
#include "real/cs2/process.hpp"
#include "real/win/api_table.hpp"
#include "real/win/timing.hpp"
#include "real/win/xorstr.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#if LR_PLATFORM_WINDOWS
#include "real/win/windows_h.hpp"
#endif

namespace real::cs2::stack {

// ── Throttle ──────────────────────────────────────────────────────
bool ReadThrottle::full_due(std::chrono::steady_clock::time_point now) {
  return now >= next_full;
}
bool ReadThrottle::local_due(std::chrono::steady_clock::time_point now) {
  return now >= next_local;
}
bool ReadThrottle::far_due(std::chrono::steady_clock::time_point now) {
  return now >= next_far;
}
namespace {

// Shared jittered re-arm. A zero/negative rate means "off": park the deadline
// far enough out that the caller's `*_due` check never fires, instead of the
// old behaviour where far_hz=0 still armed a 1000 ms cadence.
std::chrono::steady_clock::time_point arm_after(
    std::chrono::steady_clock::time_point now, int hz, int min_ms, float jitter_frac,
    std::uint64_t& rng) {
  if (hz <= 0) return now + std::chrono::seconds(10);
  const double base_ms = 1000.0 / std::max(1, hz);
  const double j =
      1.0 + jitter_frac * (static_cast<double>(xorshift(rng) % 2001) / 1000.0 - 1.0);
  const auto ms = std::max(min_ms, static_cast<int>(base_ms * j));
  return now + std::chrono::milliseconds(ms);
}

}  // namespace

void ReadThrottle::arm_full(std::chrono::steady_clock::time_point now) {
  if (rng == 0)
    rng = 0xC0FFEEu ^ static_cast<std::uint64_t>(now.time_since_epoch().count());
  next_full = arm_after(now, target_hz, 8, jitter_frac, rng);
}
void ReadThrottle::arm_local(std::chrono::steady_clock::time_point now) {
  if (rng == 0)
    rng = 0xC0FFEEu ^ static_cast<std::uint64_t>(now.time_since_epoch().count());
  // Local refresh is jittered too: a fixed 15 Hz cadence is as recognisable as
  // a fixed full-scan cadence once blue correlates timestamps.
  next_local = arm_after(now, local_hz, 8, jitter_frac, rng);
}
void ReadThrottle::arm_far(std::chrono::steady_clock::time_point now) {
  if (rng == 0)
    rng = 0xC0FFEEu ^ static_cast<std::uint64_t>(now.time_since_epoch().count());
  next_far = arm_after(now, far_hz, 20, jitter_frac, rng);
}


// ── Ladder ────────────────────────────────────────────────────────
const char* AttachLadder::stage_name(AttachStage s) noexcept {
  switch (s) {
    case AttachStage::HijackVerified: return "hijack_verified";
    case AttachStage::DonorWorkerIpc: return "donor_worker_ipc";
    case AttachStage::DirectOpenProcess: return "direct_openprocess";
    case AttachStage::DegradedHudOnly: return "degraded_hud_only";
    default: return "none";
  }
}
const char* AttachLadder::scar_string(AttachStage s) noexcept {
  switch (s) {
    case AttachStage::HijackVerified:
      return "SCAR:L1_hijack donor-duplicated VM_READ; UI may still soft-query";
    case AttachStage::DonorWorkerIpc:
      return "SCAR:L2_donor_worker pure IPC — UI detached from CS2; scar on worker PID";
    case AttachStage::DirectOpenProcess:
      return "SCAR:L3_openprocess direct PROCESS_VM_READ on cs2.exe (UI)";
    case AttachStage::DegradedHudOnly:
      return "SCAR:L4_degraded no entity RPM";
    default:
      return "SCAR:none";
  }
}
void AttachLadder::set_stage(AttachStage s) noexcept {
  stage = s;
  last_scar = scar_string(s);
  hijack_handle = (s == AttachStage::HijackVerified);
  worker_ipc = (s == AttachStage::DonorWorkerIpc);
  open_process_handle = (s == AttachStage::DirectOpenProcess);
  ui_detached = (s == AttachStage::DonorWorkerIpc);
}


// ── Streamproof ───────────────────────────────────────────────────
StreamproofAudit apply_and_audit_streamproof(void* hwnd,
                                             bool only_if_streaming) noexcept {
  StreamproofAudit a{};
#if LR_PLATFORM_WINDOWS
  a.hwnd_valid = hwnd != nullptr;
  a.streaming_apps_present =
      process_name_running("obs") || process_name_running("OBS") ||
      process_name_running("Streamlabs") || process_name_running("Discord");
  if (!hwnd) {
    a.detail = "no_hwnd";
    return a;
  }
  if (only_if_streaming && !a.streaming_apps_present) {
    a.detail = "skip_wda_no_streaming_app";
    return a;
  }
  const DWORD flags = (1u << 4) | (1u << 0);
  auto& api = real::win::g_Api();
  BOOL ok = FALSE;
  if (api.resolved && api.SetWindowDisplayAffinity)
    ok = api.SetWindowDisplayAffinity(static_cast<HWND>(hwnd), flags);
  else
    ok = SetWindowDisplayAffinity(static_cast<HWND>(hwnd), flags);
  a.wda_exclude_applied = ok != FALSE;
  a.reapply_count = 1;
  DWORD cur = 0;
  if (api.resolved && api.GetWindowDisplayAffinity &&
      api.GetWindowDisplayAffinity(static_cast<HWND>(hwnd), &cur)) {
    a.affinity_readable = true;
    a.affinity_value = cur;
  } else if (GetWindowDisplayAffinity(static_cast<HWND>(hwnd), &cur)) {
    a.affinity_readable = true;
    a.affinity_value = cur;
  }
  if (a.wda_exclude_applied && a.affinity_readable && (a.affinity_value & (1u << 4)))
    a.detail = "WDA_EXCLUDEFROMCAPTURE verified";
  else if (a.wda_exclude_applied)
    a.detail = "WDA set (affinity unreadable)";
  else
    a.detail = "SetWindowDisplayAffinity failed";
#else
  (void)hwnd;
  (void)only_if_streaming;
  a.detail = "non-windows";
#endif
  return a;
}

// ── Round / bomb / spectator intel ────────────────────────────────
RoundIntel read_round_intel(RemoteReadFn read, const Cs2Offsets& offsets,
                            const EntityReadResult* prefetched) {
  RoundIntel intel{};
  if (!read || !offsets.entity_list) {
    intel.detail = "missing read/offsets";
    return intel;
  }

  EntityReadResult owned;
  const EntityReadResult* ents = prefetched;
  if (!ents) {
    EntityWalkOptions opt{};
    opt.scatter_indices = false;
    opt.jitter_us = false;
    opt.max_controllers = 64;
    opt.include_dormant = true;
    owned = read_entity_list_scattered(read, offsets, opt);
    ents = &owned;
  }

  if (!ents->read_successful && ents->entities.empty()) {
    intel.detail = "entity walk empty";
    return intel;
  }

  for (const auto& e : ents->entities) {
    if (e.is_spectator) ++intel.spectator_count;
    if (!e.is_alive || e.dormant) continue;
    // CS2: 2 = T, 3 = CT
    if (e.team == 2) ++intel.alive_t;
    else if (e.team == 3) ++intel.alive_ct;
  }

  // Planted C4: client_base + dwPlantedC4 is a pointer to C_PlantedC4.
  std::uint64_t client_base = 0;
  if (offsets.entity_list > snapshot::globals::dwEntityList) {
    client_base = offsets.entity_list - snapshot::globals::dwEntityList;
  }
  if (client_base) {
    std::uint64_t c4_ptr_slot = client_base + snapshot::globals::dwPlantedC4;
    std::uint64_t c4 = 0;
    if (rd_t(read, c4_ptr_slot, c4) && c4 > 0x10000ull) {
      std::uint8_t ticking = 0;
      if (rd_t(read, c4 + snapshot::fields::m_bBombTicking, ticking) && ticking) {
        intel.bomb_planted = true;
        std::int32_t site = -1;
        if (rd_t(read, c4 + snapshot::fields::m_nBombSite, site)) intel.bomb_site = site;
        float blow = 0.f;
        if (rd_t(read, c4 + snapshot::fields::m_flC4Blow, blow)) intel.bomb_blow_time = blow;
        std::uint8_t defusing = 0;
        if (rd_t(read, c4 + snapshot::fields::m_bBeingDefused, defusing))
          intel.bomb_defusing = defusing != 0;
        // Origin via game scene node when present.
        std::uint64_t scene = 0;
        if (rd_t(read, c4 + snapshot::fields::m_pGameSceneNode, scene) && scene) {
          ac::Vec3 origin{};
          if (rd_t(read, scene + snapshot::fields::m_vecAbsOrigin, origin))
            intel.bomb_origin = origin;
        }
      }
    }
  }

  // Round active heuristic: any alive players or a planted bomb.
  intel.is_round = (intel.alive_t + intel.alive_ct) > 0 || intel.bomb_planted;
  intel.valid = true;
  intel.detail = intel.bomb_planted ? "round+bomb" : (intel.is_round ? "round" : "idle");
  return intel;
}

// ── Blue dual (honest scar owner) ─────────────────────────────────
BlueDualScore score_blue_dual(const BlueDualConfig& cfg) noexcept {
  BlueDualScore s{};
  s.rpm_reads_window = cfg.rpm_reads;
  s.rpm_bytes_window = cfg.rpm_bytes;

  const double sec = cfg.window_sec > 0.1 ? cfg.window_sec : 5.0;
  const double rps = static_cast<double>(cfg.rpm_reads) / sec;
  if (rps > 5000.0) s.volume_score = 95;
  else if (rps > 2000.0) s.volume_score = 70;
  else if (rps > 500.0) s.volume_score = 40;
  else if (rps > 100.0) s.volume_score = 20;
  else s.volume_score = 5;

  // Handle scar ownership honesty
  if (cfg.ui_holds_openprocess && !cfg.ui_detached) {
    s.handle_score = 90;
    s.scar_owner = "ui";
    s.scar_pid = cfg.self_pid;
    s.foreign_vm_read_handles = 1;
  } else if (cfg.has_worker) {
    s.handle_score = 70;  // scar on worker, not UI
    s.scar_owner = "worker";
    s.scar_pid = cfg.worker_pid;
    s.foreign_vm_read_handles = 1;
  } else if (cfg.has_hijack) {
    s.handle_score = 55;
    s.scar_owner = "donor";
    s.scar_pid = cfg.donor_pid;
    s.foreign_vm_read_handles = 1;
  } else {
    s.handle_score = 0;
    s.scar_owner = "none";
  }

#if LR_PLATFORM_WINDOWS
  int hits = 0;
  if (cfg.cs2_pid) ++hits;
  if (process_name_running("steam") || process_name_running("Steam")) ++hits;
  if (process_name_running("discord") || process_name_running("Discord")) ++hits;
  if (cfg.self_pid) ++hits;
  if (cfg.worker_pid) ++hits;  // multi-process red shape
  s.cooccur_hits = hits;
  if (hits >= 4) s.cooccur_score = 70;
  else if (hits >= 3) s.cooccur_score = 45;
  else s.cooccur_score = 15;
#else
  s.cooccur_score = 10;
#endif

  // Pure L2: handle score applies to worker; UI “clean” narrative still composite
  s.composite = (s.handle_score * 45 + s.volume_score * 35 + s.cooccur_score * 20) / 100;
  s.would_detect = s.composite >= 50;
  if (s.handle_score >= s.volume_score && s.handle_score >= s.cooccur_score)
    s.primary_reason = s.scar_owner;
  else if (s.volume_score >= s.cooccur_score)
    s.primary_reason = "rpm_volume";
  else
    s.primary_reason = "process_cooccurrence";
  return s;
}

// ── Offsets ───────────────────────────────────────────────────────
Cs2Offsets offsets_from_client_base(std::uint64_t client_base) noexcept {
  return offsets_from_snapshot(client_base);
}

bool write_offset_snapshot_file(const Cs2Offsets& offsets, std::uint64_t client_base,
                                const char* path) noexcept {
  if (!path || !client_base) return false;
  // Emit RVAs
  const auto rva = [&](std::uint64_t abs) -> std::uint64_t {
    return abs >= client_base ? abs - client_base : abs;
  };
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out << "{\n";
  out << "  \"dwEntityList\": " << rva(offsets.entity_list) << ",\n";
  out << "  \"dwLocalPlayerPawn\": " << rva(offsets.local_player) << ",\n";
  if (offsets.view_matrix)
    out << "  \"dwViewMatrix\": " << rva(offsets.view_matrix) << ",\n";
  if (offsets.view_angles)
    out << "  \"dwViewAngles\": " << rva(offsets.view_angles) << ",\n";
  out << "  \"m_iHealth\": " << offsets.entity_health << ",\n";
  out << "  \"m_iTeamNum\": " << offsets.entity_team << ",\n";
  out << "  \"m_lifeState\": " << offsets.entity_lifestate << ",\n";
  out << "  \"m_vOldOrigin\": " << offsets.entity_origin << ",\n";
  out << "  \"m_angEyeAngles\": " << offsets.entity_viewangles << ",\n";
  out << "  \"m_hPlayerPawn\": " << offsets.entity_controller_pawn << ",\n";
  out << "  \"entity_identity_stride\": " << offsets.entity_list_entry << "\n";
  out << "}\n";
  return true;
}

OffsetUpdateResult auto_update_offsets(std::uint32_t cs2_pid,
                                       std::uint64_t process_handle,
                                       std::uint64_t main_base,
                                       std::size_t main_image_size,
                                       Cs2Offsets& out,
                                       bool try_patterns) noexcept {
  OffsetUpdateResult r{};
  Cs2Offsets fields{};
  r.snapshot_loaded = try_load_runtime_snapshot_file(fields);

  if (process_handle != 0) {
    auto resolved = resolve_offsets_for_process(cs2_pid, process_handle, main_base,
                                                main_image_size);
    if (resolved) {
      out = *resolved;
      r.complete = out.is_complete();
      r.detail = r.snapshot_loaded ? "snapshot+resolve" : "embedded_resolve";
    } else {
      r.detail = "resolve_failed";
      return r;
    }
  } else if (main_base != 0) {
    // L2 pure: main_base treated as client_base when process_handle==0
    out = offsets_from_client_base(main_base);
    if (r.snapshot_loaded) {
      if (fields.entity_health) out.entity_health = fields.entity_health;
      if (fields.entity_team) out.entity_team = fields.entity_team;
      if (fields.entity_lifestate) out.entity_lifestate = fields.entity_lifestate;
      if (fields.entity_origin) out.entity_origin = fields.entity_origin;
      if (fields.entity_viewangles) out.entity_viewangles = fields.entity_viewangles;
      if (fields.entity_controller_pawn)
        out.entity_controller_pawn = fields.entity_controller_pawn;
      if (fields.entity_list_entry) out.entity_list_entry = fields.entity_list_entry;
    }
    r.complete = out.is_complete();
    r.detail = "l2_client_base_snapshot";
  } else {
    r.detail = "no_base";
    return r;
  }

  if (try_patterns && !r.complete && process_handle != 0) {
    r.pattern_attempted = true;
    auto scanned = resolve_offsets(cs2_pid, main_base, main_image_size);
    if (scanned) {
      // Merge any newly found globals
      if (scanned->entity_list) out.entity_list = scanned->entity_list;
      if (scanned->local_player) out.local_player = scanned->local_player;
      r.pattern_helped = out.is_complete();
      r.detail += r.pattern_helped ? ";pattern_helped" : ";pattern_partial";
    }
  }

  if (r.complete || r.pattern_helped) {
    // Write-back for patch nights
    std::uint64_t client_base = 0;
    if (out.entity_list > 0x100000) {
      // entity_list is absolute; reverse-estimate client base from known RVA
      // Prefer explicit: if process had handle, find_client_module already done.
      // Use entity_list - dwEntityList default RVA as approximation only when needed.
    }
    // Prefer writing relative to cwd; use entity_list absolute with client from snapshot path
    const char* out_path = "offsets_snapshot.json";
    // If we have absolute entity_list and known RVA from snapshot globals
    std::uint64_t cbase = 0;
    if (out.entity_list > snapshot::globals::dwEntityList)
      cbase = out.entity_list - snapshot::globals::dwEntityList;
    if (cbase && write_offset_snapshot_file(out, cbase, out_path)) {
      r.wrote_snapshot = true;
      r.write_path = out_path;
      r.detail += ";wrote_snapshot";
    }
  }
  if (r.complete) r.detail += ";complete=1";
  return r;
}

}  // namespace real::cs2::stack

