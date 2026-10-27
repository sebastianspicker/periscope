// live_radar_stack.hpp — Attach ladder, L2 pure IPC, scattered throttle,
// streamproof, blue dual scoring, forensic exit (educational).

#pragma once

#include "real/cs2/entities.hpp"
#include "real/cs2/offsets.hpp"
#include "real/cs2/posture.hpp"
#include "real/error.hpp"
#include "real/platform.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace real::cs2::stack {

// Generic remote read (OpenProcess RPM, hijack, or donor IPC).
using RemoteReadFn = bool (*)(std::uint64_t addr, void* buf, std::size_t size);

// ── Read throttle + priority ─────────────────────────────────────
enum class EntityPriority : int {
  Local = 0,
  SpottedNear = 1,
  AliveFar = 2,
  DormantOrDead = 3,
};

struct ReadThrottle {
  int target_hz = 30;            // full priority-0/1 scans
  int local_hz = 60;
  int far_hz = 8;                // dormant/far controllers
  float jitter_frac = 0.15f;
  std::uint64_t rng = 0xC0FFEEu;

  std::chrono::steady_clock::time_point next_full{};
  std::chrono::steady_clock::time_point next_local{};
  std::chrono::steady_clock::time_point next_far{};

  bool full_due(std::chrono::steady_clock::time_point now);
  bool local_due(std::chrono::steady_clock::time_point now);
  bool far_due(std::chrono::steady_clock::time_point now);
  void arm_full(std::chrono::steady_clock::time_point now);
  void arm_local(std::chrono::steady_clock::time_point now);
  void arm_far(std::chrono::steady_clock::time_point now);
};

// ── Scattered entity walk ────────────────────────────────────────
struct EntityWalkOptions {
  bool scatter_indices = true;   // shuffle 1..N BEFORE resolve
  bool jitter_us = true;
  int max_controllers = 64;
  /// When true, only indices that previously yielded alive/spotted (sticky).
  bool priority_filter = false;
  bool include_dormant = false;
  std::uint64_t* rng = nullptr;
};

// ── Attach ladder ────────────────────────────────────────────────
enum class AttachStage : int {
  None = 0,
  HijackVerified = 1,
  DonorWorkerIpc = 2,
  DirectOpenProcess = 3,
  DegradedHudOnly = 4,
};

struct AttachLadder {
  AttachStage stage = AttachStage::None;
  std::uint32_t cs2_pid = 0;
  std::uint32_t donor_pid = 0;
  std::uint32_t worker_pid = 0;
  bool open_process_handle = false;  // UI holds OpenProcess
  bool hijack_handle = false;
  bool worker_ipc = false;
  bool ui_detached = false;          // pure L2: UI has no CS2 handle
  const char* last_scar = "not attached";

  static const char* stage_name(AttachStage s) noexcept;
  static const char* scar_string(AttachStage s) noexcept;
  void set_stage(AttachStage s) noexcept;
};

// ── Donor worker IPC v2 (modules published; UI never opens CS2) ──
#pragma pack(push, 1)
struct DonorIpcHeader {
  std::uint32_t magic = 0;
  std::uint32_t version = 2;
  std::uint32_t cs2_pid = 0;
  std::uint32_t worker_pid = 0;
  std::uint32_t ticket = 0;
  std::uint32_t status = 0;      // 0 idle 1 busy 2 ok 3 err
  std::uint64_t req_addr = 0;
  std::uint32_t req_size = 0;
  std::uint32_t resp_size = 0;
  std::uint32_t seq = 0;
  std::uint32_t flags = 0;       // bit0 = modules_ready
  // v2 module bases (worker-only discovery)
  std::uint64_t client_base = 0;
  std::uint64_t engine2_base = 0;
  std::uint64_t tier0_base = 0;
  std::uint64_t cs2_base = 0;
  std::uint32_t client_size = 0;
  std::uint32_t engine2_size = 0;
  std::uint32_t tier0_size = 0;
  std::uint32_t cs2_image_size = 0;
};
#pragma pack(pop)

constexpr std::uint32_t kDonorMagic = 0x57444C52u;
constexpr std::uint32_t kDonorVersion = 2;
constexpr std::uint32_t kDonorFlagModulesReady = 1u;
// Must cover pattern-scan chunk (0x10000) for c_hud AOB over IPC.
constexpr std::uint32_t kDonorMaxPayload = 0x10000;
constexpr const char* kDonorMapName = "Local\\LR_DonorWorker_v2";
constexpr const char* kDonorEventReq = "Local\\LR_DonorWorker_Req_v2";
constexpr const char* kDonorEventRsp = "Local\\LR_DonorWorker_Rsp_v2";

struct DonorModules {
  std::uint64_t client_base = 0;
  std::uint64_t engine2_base = 0;
  std::uint64_t tier0_base = 0;
  std::uint64_t cs2_base = 0;
  std::size_t client_size = 0;
  std::size_t engine2_size = 0;
  std::size_t tier0_size = 0;
  std::size_t cs2_image_size = 0;
  bool ready = false;
};

class DonorIpcClient {
 public:
  bool connect() noexcept;
  void disconnect() noexcept;
  bool is_connected() const noexcept { return mapped_ != nullptr; }
  bool read(std::uint64_t addr, void* buf, std::size_t size) noexcept;
  std::uint32_t worker_pid() const noexcept;
  std::uint32_t cs2_pid() const noexcept;
  DonorModules modules() const noexcept;
  /// Wait until worker publishes module bases (ms).
  bool wait_modules(int timeout_ms = 2000) noexcept;

 private:
  void* map_handle_ = nullptr;
  void* mapped_ = nullptr;
  void* evt_req_ = nullptr;
  void* evt_rsp_ = nullptr;
};

std::uint32_t spawn_donor_worker() noexcept;

/// Wipe IPC objects (forensic_cleanup_exit).
void forensic_cleanup_ipc() noexcept;

/// Best-effort process disguise (console title / window) — educational.
void apply_process_disguise(const char* role) noexcept;

// ── Streamproof ──────────────────────────────────────────────────
struct StreamproofAudit {
  bool wda_exclude_applied = false;
  bool affinity_readable = false;
  std::uint32_t affinity_value = 0;
  bool streaming_apps_present = false;
  bool hwnd_valid = false;
  int reapply_count = 0;
  const char* detail = "not audited";
};

StreamproofAudit apply_and_audit_streamproof(void* hwnd,
                                             bool only_if_streaming = false) noexcept;

// ── Round / bomb / spectator ─────────────────────────────────────
struct RoundIntel {
  bool is_round = false;
  bool bomb_planted = false;
  bool bomb_defusing = false;
  int bomb_site = -1;
  float bomb_blow_time = 0.f;    // m_flC4Blow (game time) when readable
  ac::Vec3 bomb_origin{};
  int spectator_count = 0;       // observer mode != none
  int alive_t = 0;
  int alive_ct = 0;
  float map_texture_scale = 0.f;
  float radar_scale = 0.f;
  bool valid = false;
  const char* detail = "not collected";
};

/// Derive round/bomb/spectator intel from a scattered entity walk + planted C4.
/// Pure remote-read path (works with hijack / donor IPC / RPM bridge).
RoundIntel read_round_intel(RemoteReadFn read, const Cs2Offsets& offsets,
                            const EntityReadResult* prefetched = nullptr);

// ── Blue dual (honest about scar owner) ──────────────────────────
struct BlueDualScore {
  int handle_score = 0;
  int volume_score = 0;
  int cooccur_score = 0;
  int composite = 0;
  bool would_detect = false;
  const char* primary_reason = "clean";
  const char* scar_owner = "none";  // "ui" | "worker" | "donor" | "none"
  std::uint32_t scar_pid = 0;
  std::uint64_t rpm_reads_window = 0;
  std::uint64_t rpm_bytes_window = 0;
  int foreign_vm_read_handles = 0;
  int cooccur_hits = 0;
};

struct BlueDualConfig {
  std::uint32_t cs2_pid = 0;
  std::uint32_t self_pid = 0;
  std::uint32_t worker_pid = 0;
  std::uint32_t donor_pid = 0;
  std::uint64_t rpm_reads = 0;
  std::uint64_t rpm_bytes = 0;
  double window_sec = 5.0;
  bool ui_holds_openprocess = false;
  bool has_hijack = false;
  bool has_worker = false;
  bool ui_detached = false;
};

BlueDualScore score_blue_dual(const BlueDualConfig& cfg) noexcept;

// ── Intel exposure + stealth shaping (Oct-2026 posture) ──────────
// The policy itself lives in real/cs2/posture.hpp so it stays pure and
// host-testable: IntelMode, IntelPolicy, BehaviorShaper, DegradeLadder and the
// apply_posture composition. Ordering is load-bearing — the event shaper raises
// the floor and the degrade ladder caps it afterwards, otherwise the per-frame
// shaper would wipe out any degrade on the next frame.
using IntelMode = real::cs2::stack::IntelMode;
using IntelPolicy = real::cs2::stack::IntelPolicy;
using BehaviorShaper = real::cs2::stack::BehaviorShaper;
using DegradeLadder = real::cs2::stack::DegradeLadder;
using Posture = real::cs2::stack::Posture;
using ReadRates = real::cs2::stack::ReadRates;
using RoundEvents = real::cs2::stack::RoundEvents;

/// Project world round intel onto the pure policy's event view.
inline RoundEvents round_events_of(const RoundIntel& intel) noexcept {
  RoundEvents ev;
  ev.valid = intel.valid;
  ev.is_round = intel.is_round;
  ev.bomb_planted = intel.bomb_planted;
  ev.bomb_defusing = intel.bomb_defusing;
  return ev;
}

/// Copy resolved rates onto the live throttle.
inline void apply_rates(ReadThrottle& t, const ReadRates& r) noexcept {
  t.target_hz = r.full_hz;
  t.local_hz = r.local_hz;
  t.far_hz = r.far_hz;
}

// ── Offset auto-update + write-back ──────────────────────────────
struct OffsetUpdateResult {
  bool snapshot_loaded = false;
  bool pattern_attempted = false;
  bool pattern_helped = false;
  bool wrote_snapshot = false;
  bool complete = false;
  std::string detail;
  std::string write_path;
};

OffsetUpdateResult auto_update_offsets(std::uint32_t cs2_pid,
                                       std::uint64_t process_handle,
                                       std::uint64_t main_base,
                                       std::size_t main_image_size,
                                       Cs2Offsets& out,
                                       bool try_patterns = true) noexcept;

/// Build offsets from client_base alone (L2 pure path, no process handle).
Cs2Offsets offsets_from_client_base(std::uint64_t client_base) noexcept;

/// Write runtime snapshot JSON (patch-night write-back).
bool write_offset_snapshot_file(const Cs2Offsets& offsets,
                                std::uint64_t client_base,
                                const char* path = "offsets_snapshot.json") noexcept;

// ── Entity list: scattered walk via RemoteReadFn ─────────────────
EntityReadResult read_entity_list_scattered(RemoteReadFn read,
                                            const Cs2Offsets& offsets,
                                            const EntityWalkOptions& opt);

Result<Cs2PlayerEntity> read_local_player_fn(RemoteReadFn read,
                                             const Cs2Offsets& offsets);

// Thin adapter so existing Cs2MemoryReader call sites can share walk.
EntityReadResult read_entity_list_throttled(Cs2MemoryReader& reader,
                                            const Cs2Offsets& offsets,
                                            const EntityWalkOptions& opt,
                                            std::uint32_t local_pid = 0);

}  // namespace real::cs2::stack
