// entities.hpp — Real CS2 entity list reader.
//
// LESSON: Reading the entity list is the primary data source for radar
// cheats. Red reads player positions, team, health, and other state
// from the game's entity table. Blue detects this via:
//   - Handle graph (T0): who has VM_READ on the game?
//   - Read volume (T0): abnormal RPM byte counts
//   - Pattern scans (T0): sequential entity table reads
//   - Timing analysis (T0): entity polling frequency
//
// Educational design:
//   REAL MODE:   Reads the real CS2 entity list from process memory.
//   SIM MODE:    Reads sim::World's synthetic entity table.
//   Both modes produce identical EntitySnapshot structures for radar
//   rendering and analysis. The lesson is in HOW the data is acquired,
//   not the data itself.

#pragma once

#include "ac/types.hpp"
#include "real/cs2/memory.hpp"
#include "real/cs2/offsets.hpp"
#include "real/error.hpp"
#include "real/mode/mode.hpp"
#include "real/platform.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace real::cs2 {

/// A single player entity read from the real CS2 process.
/// Mirror of ac::EntitySnapshot with CS2-specific fields.
struct Cs2PlayerEntity {
  std::uint32_t controller_handle = 0;
  std::uint32_t pawn_handle = 0;
  std::uint32_t team = 0;
  std::int32_t health = 0;
  std::int32_t armor = 0;
  int32_t life_state = 0;
  ac::Vec3 origin{};
  ac::Vec3 eye_angles{};
  bool is_alive = false;
  bool is_local_player = false;
  bool dormant = false;
  bool spotted = true;   ///< m_bSpotted value (fail-open default)
  bool spotted_known = false;  ///< false when the spotted read failed
  bool is_bomb = false;  ///< planted C4 pseudo-entity
  bool is_hostage = false;
  bool is_spectator = false;  ///< observer mode != none
  int observer_mode = 0;
  int bomb_site = -1;
  float bomb_blow_time = 0.f;
  bool bomb_defusing = false;

  /// Convert to the generic ac::EntitySnapshot used by radar rendering.
  ac::EntitySnapshot to_snapshot() const;
};

/// Result of an entity list read operation.
struct EntityReadResult {
  std::vector<Cs2PlayerEntity> entities;
  int entity_count = 0;
  int local_player_index = -1;
  bool read_successful = false;
  std::string error_msg;
  std::uint64_t read_time_us = 0;  ///< Microseconds spent reading

  std::string describe() const;
};

/// Read the CS2 entity list from the real process.
/// Resolves the entity list pointer, walks the linked list or array,
/// and returns structured player data.
///
/// REAL MODE:   Reads from cs2.exe process memory via the tier backend.
/// SIM MODE:    Reads from sim::World's synthetic memory.
///
/// Parameters:
///   reader   - Attached Cs2MemoryReader
///   offsets  - Resolved CS2 offsets (from resolve_offsets or sim_offsets)
///   local_pid - The local player's PID (for is_local detection)
EntityReadResult read_entity_list(Cs2MemoryReader& reader,
                                   const Cs2Offsets& offsets,
                                   std::uint32_t local_pid = 0);

/// Read the local player's data specifically.
/// Faster than a full entity list scan when only local data is needed.
Result<Cs2PlayerEntity> read_local_player(Cs2MemoryReader& reader,
                                           const Cs2Offsets& offsets);

}  // namespace real::cs2
