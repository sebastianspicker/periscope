#include "real/cs2/entities.hpp"
#include "real/cs2/offsets_snapshot.hpp"
#include "real/win/xorstr.hpp"

// Schema field RVAs for filters/polish (see offsets_snapshot.hpp).

#include <chrono>
#include <cstring>
#include <sstream>

namespace real::cs2 {
namespace {

template <typename T>
Result<T> read_value(Cs2MemoryReader& reader, std::uint64_t address,
                     const char* description) {
  const auto read = reader.read(address, sizeof(T));
  if (read.status != ac::Status::Ok || read.bytes.size() != sizeof(T)) {
    return {{}, std::string("Failed to read ") + description};
  }

  T value{};
  std::memcpy(&value, read.bytes.data(), sizeof(value));
  return Result<T>(value);
}

// Source 2 chunked entity identity system.
std::uint32_t handle_index_mask() {
  return snapshot::constants::entity_handle_index_mask;
}
std::uint32_t chunk_shift() {
  return snapshot::constants::entity_chunk_shift;
}
std::uint64_t chunk_table_offset() {
  return snapshot::constants::entity_chunk_table_offset;
}
std::uint64_t identity_stride(const Cs2Offsets& offsets) {
  return offsets.entity_list_entry != 0
             ? offsets.entity_list_entry
             : snapshot::constants::entity_identity_stride;
}

Result<std::uint64_t> resolve_handle(Cs2MemoryReader& reader,
                                     std::uint64_t entity_list,
                                     std::uint32_t handle,
                                     const Cs2Offsets& offsets) {
  const std::uint32_t index = handle & handle_index_mask();
  if (index == 0) return {{}, OBF("Entity handle is null")};

  const std::uint64_t chunk_address =
      entity_list + chunk_table_offset() +
      static_cast<std::uint64_t>(index >> chunk_shift()) * sizeof(std::uint64_t);
  const auto chunk = read_value<std::uint64_t>(reader, chunk_address, OBF("entity-list chunk"));
  if (!chunk || *chunk == 0) {
    return {{}, chunk.error_msg.empty() ? OBF("Entity-list chunk is unavailable")
                                        : chunk.error_msg.c_str()};
  }

  const std::uint64_t stride = identity_stride(offsets);
  const std::uint64_t slot =
      *chunk + static_cast<std::uint64_t>(index & ((1u << chunk_shift()) - 1u)) * stride;
  // First pointer in identity slot is the entity object.
  return read_value<std::uint64_t>(reader, slot, OBF("entity handle entry"));
}

Result<Cs2PlayerEntity> read_player_at(Cs2MemoryReader& reader,
                                        const Cs2Offsets& offsets,
                                        std::uint64_t entity_list,
                                        std::uint64_t controller,
                                        std::uint32_t controller_handle,
                                        std::uint64_t local_pawn) {
  if (controller == 0 || offsets.entity_controller_pawn == 0) {
    return {{}, OBF("Controller or controller-to-pawn offset is unavailable")};
  }

  const auto pawn_handle = read_value<std::uint32_t>(
      reader, controller + offsets.entity_controller_pawn, OBF("controller pawn handle"));
  if (!pawn_handle || *pawn_handle == 0) {
    return {{}, pawn_handle.error_msg.empty() ? OBF("Player has no pawn")
                                              : pawn_handle.error_msg.c_str()};
  }
  const auto pawn = resolve_handle(reader, entity_list, *pawn_handle, offsets);
  if (!pawn || *pawn == 0) {
    return {{}, pawn.error_msg.empty() ? OBF("Pawn handle did not resolve")
                                       : pawn.error_msg.c_str()};
  }

  Cs2PlayerEntity entity;
  entity.controller_handle = controller_handle;
  entity.pawn_handle = *pawn_handle;

  // m_iTeamNum is uint8 in schema; read 4 bytes and mask.
  const auto team = read_value<std::uint32_t>(reader, *pawn + offsets.entity_team,
                                              OBF("m_iTeamNum"));
  if (!team) return {{}, team.error_msg};
  entity.team = (*team) & 0xFFu;

  const auto health = read_value<std::int32_t>(reader, *pawn + offsets.entity_health,
                                               OBF("m_iHealth"));
  if (!health) return {{}, health.error_msg};
  entity.health = *health;

  const auto life_state = read_value<std::uint8_t>(
      reader, *pawn + offsets.entity_lifestate, OBF("m_lifeState"));
  if (!life_state) return {{}, life_state.error_msg};
  entity.life_state = *life_state;

  const auto origin = read_value<ac::Vec3>(reader, *pawn + offsets.entity_origin,
                                           OBF("m_vOldOrigin"));
  if (!origin) return {{}, origin.error_msg};
  entity.origin = *origin;

  entity.is_alive = entity.health > 0 && entity.life_state == 0;
  entity.is_local_player = (local_pawn != 0 && *pawn == local_pawn);

  if (offsets.entity_viewangles != 0) {
    const auto angles = read_value<ac::Vec3>(reader, *pawn + offsets.entity_viewangles,
                                             OBF("m_angEyeAngles"));
    if (angles) entity.eye_angles = *angles;
  }

  // Armor (optional — best-effort).
  {
    const auto armor = read_value<std::int32_t>(
        reader, *pawn + snapshot::fields::m_ArmorValue, OBF("m_ArmorValue"));
    if (armor) entity.armor = *armor;
  }

  // Dormant: CGameSceneNode::m_bDormant via m_pGameSceneNode.
  {
    const auto scene = read_value<std::uint64_t>(
        reader, *pawn + snapshot::fields::m_pGameSceneNode, OBF("m_pGameSceneNode"));
    if (scene && *scene != 0) {
      const auto dorm = read_value<std::uint8_t>(
          reader, *scene + snapshot::fields::m_bDormant, OBF("m_bDormant"));
      if (dorm) entity.dormant = (*dorm) != 0;
    }
  }

  // Spotted: EntitySpottedState_t at m_entitySpottedState + m_bSpotted.
  {
    const auto spotted = read_value<std::uint8_t>(
        reader,
        *pawn + snapshot::fields::m_entitySpottedState + snapshot::fields::m_bSpotted,
        OBF("m_bSpotted"));
    if (spotted) {
      entity.spotted = (*spotted) != 0;
      entity.spotted_known = true;
    } else {
      // Fail open for the radar, but record the unknown so strict intel can
      // hide instead of leaking an unspotted enemy through hud-echo.
      entity.spotted = true;
      entity.spotted_known = false;
    }
  }

  return Result<Cs2PlayerEntity>(entity);
}

}  // namespace

ac::EntitySnapshot Cs2PlayerEntity::to_snapshot() const {
  ac::EntitySnapshot s{};
  s.origin = origin;
  s.eye_angles = eye_angles;
  s.health = health;
  s.team = static_cast<std::uint8_t>(team);
  s.alive = is_alive;
  s.is_local_player = is_local_player;
  s.dormant = dormant;
  return s;
}

std::string EntityReadResult::describe() const {
  std::ostringstream out;
  out << "entities=" << entity_count << " local_index=" << local_player_index
      << " successful=" << (read_successful ? "true" : "false")
      << " read_time_us=" << read_time_us;
  if (!error_msg.empty()) out << " error=" << error_msg;
  return out.str();
}

EntityReadResult read_entity_list(Cs2MemoryReader& reader,
                                  const Cs2Offsets& offsets,
                                  std::uint32_t /*local_pid*/) {
  EntityReadResult result;
  const auto start = std::chrono::high_resolution_clock::now();

#if LR_PLATFORM_WINDOWS
  // dwEntityList is the address of a global pointer in client.dll.
  const auto entity_list_ptr =
      read_value<std::uint64_t>(reader, offsets.entity_list, OBF("entity list pointer"));
  if (!entity_list_ptr || *entity_list_ptr == 0) {
    result.error_msg = entity_list_ptr.error_msg.empty()
                           ? OBF("Failed to read entity list pointer")
                           : entity_list_ptr.error_msg.c_str();
  } else {
    const std::uint64_t entity_list = *entity_list_ptr;

    // dwLocalPlayerPawn -> pawn object pointer
    std::uint64_t local_pawn = 0;
    if (offsets.local_player != 0) {
      const auto local = read_value<std::uint64_t>(reader, offsets.local_player,
                                                   OBF("local player pawn"));
      if (local) local_pawn = *local;
    }

    // Controllers occupy low entity indices (1..64) in the identity system.
    constexpr int kMaxControllers = 64;
    for (int index = 1; index <= kMaxControllers; ++index) {
      const auto controller =
          resolve_handle(reader, entity_list, static_cast<std::uint32_t>(index), offsets);
      if (!controller || *controller == 0) continue;

      const auto entity = read_player_at(reader, offsets, entity_list, *controller,
                                         static_cast<std::uint32_t>(index), local_pawn);
      if (!entity) continue;
      if ((*entity).is_local_player) result.local_player_index = result.entity_count;
      result.entities.push_back(*entity);
      ++result.entity_count;
    }
    result.read_successful = true;
  }
#else
  (void)reader;
  (void)offsets;
  result.error_msg = "Real CS2 entity reads are supported only on Windows";
#endif

  result.read_time_us = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::high_resolution_clock::now() - start).count());
  return result;
}

Result<Cs2PlayerEntity> read_local_player(Cs2MemoryReader& reader,
                                          const Cs2Offsets& offsets) {
#if LR_PLATFORM_WINDOWS
  const auto local = read_value<std::uint64_t>(reader, offsets.local_player,
                                               OBF("local player pointer"));
  if (!local || *local == 0) {
    return {{}, local.error_msg.empty() ? OBF("Local player is unavailable")
                                        : local.error_msg.c_str()};
  }

  Cs2PlayerEntity entity;
  entity.pawn_handle = static_cast<std::uint32_t>(*local);
  entity.is_local_player = true;

  const auto team = read_value<std::uint32_t>(reader, *local + offsets.entity_team, OBF("m_iTeamNum"));
  const auto health = read_value<std::int32_t>(reader, *local + offsets.entity_health, OBF("m_iHealth"));
  const auto life = read_value<std::uint8_t>(reader, *local + offsets.entity_lifestate, OBF("m_lifeState"));
  const auto origin = read_value<ac::Vec3>(reader, *local + offsets.entity_origin, OBF("m_vOldOrigin"));
  if (!team) return {{}, team.error_msg};
  if (!health) return {{}, health.error_msg};
  if (!life) return {{}, life.error_msg};
  if (!origin) return {{}, origin.error_msg};
  entity.team = (*team) & 0xFFu;
  entity.health = *health;
  entity.life_state = *life;
  entity.origin = *origin;
  entity.is_alive = entity.health > 0 && entity.life_state == 0;
  if (offsets.entity_viewangles != 0) {
    const auto angles = read_value<ac::Vec3>(reader, *local + offsets.entity_viewangles,
                                             OBF("m_angEyeAngles"));
    if (angles) entity.eye_angles = *angles;
  }
  return Result<Cs2PlayerEntity>(entity);
#else
  (void)reader;
  (void)offsets;
  return {{}, "Real CS2 local-player reads are supported only on Windows"};
#endif
}

}  // namespace real::cs2
