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

// ── Scattered entity walk ─────────────────────────────────────────
namespace {

std::uint32_t handle_index_mask() {
  return snapshot::constants::entity_handle_index_mask;
}
std::uint32_t chunk_shift() { return snapshot::constants::entity_chunk_shift; }
std::uint64_t chunk_table_offset() {
  return snapshot::constants::entity_chunk_table_offset;
}
std::uint64_t identity_stride(const Cs2Offsets& o) {
  return o.entity_list_entry ? o.entity_list_entry
                             : snapshot::constants::entity_identity_stride;
}

bool resolve_handle_fn(RemoteReadFn fn, std::uint64_t entity_list, std::uint32_t handle,
                       const Cs2Offsets& offsets, std::uint64_t& out) {
  const std::uint32_t index = handle & handle_index_mask();
  if (index == 0) return false;
  const std::uint64_t chunk_address =
      entity_list + chunk_table_offset() +
      static_cast<std::uint64_t>(index >> chunk_shift()) * 8ull;
  std::uint64_t chunk = 0;
  if (!rd_t(fn, chunk_address, chunk) || !chunk) return false;
  const std::uint64_t stride = identity_stride(offsets);
  const std::uint64_t slot =
      chunk + static_cast<std::uint64_t>(index & ((1u << chunk_shift()) - 1u)) * stride;
  return rd_t(fn, slot, out) && out != 0;
}

bool read_player_fn(RemoteReadFn fn, const Cs2Offsets& offsets, std::uint64_t entity_list,
                    std::uint64_t controller, std::uint32_t controller_handle,
                    std::uint64_t local_pawn, Cs2PlayerEntity& entity) {
  if (!controller || !offsets.entity_controller_pawn) return false;
  std::uint32_t pawn_handle = 0;
  if (!rd_t(fn, controller + offsets.entity_controller_pawn, pawn_handle) ||
      !pawn_handle)
    return false;
  std::uint64_t pawn = 0;
  if (!resolve_handle_fn(fn, entity_list, pawn_handle, offsets, pawn)) return false;

  entity = {};
  entity.controller_handle = controller_handle;
  entity.pawn_handle = pawn_handle;

  // Pack health (0x34C) + lifeState (0x354) in one IPC ticket when possible.
  std::uint32_t team = 0;
  std::int32_t health = 0;
  std::uint8_t life = 0;
  ac::Vec3 origin{};
  if (!rd_t(fn, pawn + offsets.entity_team, team)) return false;
  if (offsets.entity_lifestate == offsets.entity_health + 8) {
    std::uint8_t blob[16]{};
    if (!rd(fn, pawn + offsets.entity_health, blob, 12)) return false;
    std::memcpy(&health, blob, 4);
    life = blob[8];
  } else {
    if (!rd_t(fn, pawn + offsets.entity_health, health)) return false;
    if (!rd_t(fn, pawn + offsets.entity_lifestate, life)) return false;
  }
  // Early-out dead non-local before origin/angles (big IPC save).
  entity.team = team & 0xFFu;
  entity.health = health;
  entity.life_state = life;
  entity.is_alive = health > 0 && life == 0;
  entity.is_local_player = (local_pawn != 0 && pawn == local_pawn);
  if (!entity.is_alive && !entity.is_local_player) {
    // Still need origin for dead teammate spectator heuristics — cheap path
    if (!rd_t(fn, pawn + offsets.entity_origin, origin)) return false;
    entity.origin = origin;
    // Observer services only
    std::uint64_t obs = 0;
    if (rd_t(fn, pawn + snapshot::fields::m_pObserverServices, obs) && obs) {
      std::int32_t mode = 0;
      if (rd_t(fn, obs + snapshot::fields::m_iObserverMode, mode)) {
        entity.observer_mode = mode;
        entity.is_spectator = mode > 0;
        if (entity.is_spectator) entity.life_state = 2;
      }
    }
    return true;
  }
  if (!rd_t(fn, pawn + offsets.entity_origin, origin)) return false;
  entity.origin = origin;

  if (offsets.entity_viewangles) {
    ac::Vec3 ang{};
    if (rd_t(fn, pawn + offsets.entity_viewangles, ang)) entity.eye_angles = ang;
  }
  {
    std::int32_t armor = 0;
    if (rd_t(fn, pawn + snapshot::fields::m_ArmorValue, armor)) entity.armor = armor;
  }
  {
    std::uint64_t scene = 0;
    if (rd_t(fn, pawn + snapshot::fields::m_pGameSceneNode, scene) && scene) {
      std::uint8_t dorm = 0;
      if (rd_t(fn, scene + snapshot::fields::m_bDormant, dorm)) entity.dormant = dorm != 0;
    }
  }
  {
    std::uint8_t spotted = 1;
    if (rd_t(fn,
             pawn + snapshot::fields::m_entitySpottedState + snapshot::fields::m_bSpotted,
             spotted)) {
      entity.spotted = spotted != 0;
      entity.spotted_known = true;
    } else {
      // Unknown is not the same as spotted: strict intel hides on unknown.
      entity.spotted = true;
      entity.spotted_known = false;
    }
  }

  // Spectator: m_pObserverServices + m_iObserverMode (schema)
  {
    std::uint64_t obs = 0;
    if (rd_t(fn, pawn + snapshot::fields::m_pObserverServices, obs) && obs) {
      std::int32_t mode = 0;
      if (rd_t(fn, obs + snapshot::fields::m_iObserverMode, mode)) {
        entity.observer_mode = mode;
        entity.is_spectator = (mode > 0);
        if (entity.is_spectator && !entity.is_alive) entity.life_state = 2;
      }
    }
  }
  return true;
}

}  // namespace

EntityReadResult read_entity_list_scattered(RemoteReadFn read,
                                            const Cs2Offsets& offsets,
                                            const EntityWalkOptions& opt) {
  EntityReadResult result;
  const auto start = std::chrono::high_resolution_clock::now();
  if (!read || !offsets.entity_list) {
    result.error_msg = "missing read/offsets";
    return result;
  }

  std::uint64_t entity_list_ptr = 0;
  if (!rd_t(read, offsets.entity_list, entity_list_ptr) || !entity_list_ptr) {
    result.error_msg = "entity list pointer";
    return result;
  }
  const std::uint64_t entity_list = entity_list_ptr;

  std::uint64_t local_pawn = 0;
  if (offsets.local_player)
    rd_t(read, offsets.local_player, local_pawn);

  const int n = std::max(1, std::min(opt.max_controllers, 64));
  std::vector<int> order;
  order.reserve(static_cast<size_t>(n));
  for (int i = 1; i <= n; ++i) order.push_back(i);

  // Scatter: Fisher-Yates on controller indices BEFORE resolve
  if (opt.scatter_indices && order.size() > 1) {
    std::uint64_t rng = opt.rng ? *opt.rng : 0x9E3779B97F4A7C15ULL;
    for (size_t i = order.size(); i > 1; --i) {
      rng = rng * 0x5851F42D4C957F2DULL + 0x14057B7EF767814FULL;
      size_t j = static_cast<size_t>(rng % i);
      std::swap(order[i - 1], order[j]);
    }
    if (opt.rng) *opt.rng = rng;
  }

  int steps = 0;
  for (int index : order) {
    std::uint64_t controller = 0;
    if (!resolve_handle_fn(read, entity_list, static_cast<std::uint32_t>(index), offsets,
                           controller))
      continue;
    Cs2PlayerEntity ent{};
    if (!read_player_fn(read, offsets, entity_list, controller,
                        static_cast<std::uint32_t>(index), local_pawn, ent))
      continue;
    if (!opt.include_dormant && ent.dormant && !ent.is_local_player) continue;
    if (ent.is_local_player) result.local_player_index = result.entity_count;
    result.entities.push_back(ent);
    ++result.entity_count;
    if (opt.jitter_us && (++steps % 8) == 0) real::win::yield();
  }
  result.read_successful = true;
  result.read_time_us = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::high_resolution_clock::now() - start)
          .count());
  return result;
}

Result<Cs2PlayerEntity> read_local_player_fn(RemoteReadFn read,
                                             const Cs2Offsets& offsets) {
  if (!read || !offsets.local_player)
    return {{}, "missing"};
  std::uint64_t local = 0;
  if (!rd_t(read, offsets.local_player, local) || !local)
    return {{}, "local unavailable"};
  Cs2PlayerEntity entity{};
  entity.pawn_handle = static_cast<std::uint32_t>(local);
  entity.is_local_player = true;
  std::uint32_t team = 0;
  std::int32_t health = 0;
  std::uint8_t life = 0;
  ac::Vec3 origin{};
  if (!rd_t(read, local + offsets.entity_team, team)) return {{}, "team"};
  if (!rd_t(read, local + offsets.entity_health, health)) return {{}, "health"};
  if (!rd_t(read, local + offsets.entity_lifestate, life)) return {{}, "life"};
  if (!rd_t(read, local + offsets.entity_origin, origin)) return {{}, "origin"};
  entity.team = team & 0xFFu;
  entity.health = health;
  entity.life_state = life;
  entity.origin = origin;
  entity.is_alive = health > 0 && life == 0;
  if (offsets.entity_viewangles) {
    ac::Vec3 ang{};
    if (rd_t(read, local + offsets.entity_viewangles, ang)) entity.eye_angles = ang;
  }
  return Result<Cs2PlayerEntity>(entity);
}

EntityReadResult read_entity_list_throttled(Cs2MemoryReader& reader,
                                            const Cs2Offsets& offsets,
                                            const EntityWalkOptions& opt,
                                            std::uint32_t /*local_pid*/) {
  // Bridge: use reader.read via static TLS fn
  thread_local Cs2MemoryReader* tls = nullptr;
  tls = &reader;
  auto bridge = [](std::uint64_t a, void* b, std::size_t n) -> bool {
    if (!tls) return false;
    auto rr = tls->read(a, n);
    if (rr.status != ac::Status::Ok || rr.bytes.size() != n) return false;
    std::memcpy(b, rr.bytes.data(), n);
    return true;
  };
  return read_entity_list_scattered(bridge, offsets, opt);
}

}  // namespace real::cs2::stack

