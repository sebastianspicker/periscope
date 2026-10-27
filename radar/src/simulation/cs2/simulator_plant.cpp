#include "cs2/simulator.hpp"
#include "cs2/schema.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <limits>
#include <string>
#include <type_traits>

#include "simulation/obfuscation.hpp"

namespace cs2 {

namespace {

constexpr uintptr_t kEntityChunkArea = 0x100000;
constexpr uintptr_t kEntityChunkSize =
    static_cast<uintptr_t>(ENTITIES_PER_INNER) * ENTITY_IDENTITY_STRIDE;
constexpr std::size_t kGameMemorySize = 0x800000;
constexpr std::size_t kMaxPlayerName = 128;

bool ensure_size(std::vector<std::uint8_t>& memory, uintptr_t offset,
                 std::size_t size) {
  if (offset > std::numeric_limits<std::size_t>::max()) return false;
  const auto start = static_cast<std::size_t>(offset);
  if (size > std::numeric_limits<std::size_t>::max() - start) return false;
  const auto end = start + size;
  if (memory.size() < end) memory.resize(end, 0);
  return true;
}

bool ensure_size(std::vector<std::uint8_t>& memory, std::size_t size) {
  return ensure_size(memory, 0, size);
}

template <typename T>
void write_object(std::vector<std::uint8_t>& memory, uintptr_t offset,
                  const T& value) {
  static_assert(std::is_trivially_copyable_v<T>);
  if (!ensure_size(memory, offset, sizeof(T))) return;
  const auto bytes = std::bit_cast<std::array<std::uint8_t, sizeof(T)>>(value);
  std::copy(bytes.begin(), bytes.end(), memory.begin() +
                                      static_cast<std::ptrdiff_t>(offset));
}

void write_ptr(std::vector<std::uint8_t>& memory, uintptr_t offset,
               std::uint64_t value) {
  write_object(memory, offset, value);
}

void write_int(std::vector<std::uint8_t>& memory, uintptr_t offset, int value) {
  write_object(memory, offset, value);
}

void write_u8(std::vector<std::uint8_t>& memory, uintptr_t offset,
              std::uint8_t value) {
  if (!ensure_size(memory, offset, 1)) return;
  memory[offset] = value;
}

void write_float(std::vector<std::uint8_t>& memory, uintptr_t offset,
                 float value) {
  write_object(memory, offset, value);
}

void write_vec3(std::vector<std::uint8_t>& memory, uintptr_t offset,
                const Vector3& value) {
  write_float(memory, offset, value.x);
  write_float(memory, offset + 4, value.y);
  write_float(memory, offset + 8, value.z);
}

void write_bytes(std::vector<std::uint8_t>& memory, uintptr_t offset,
                 const void* data, std::size_t size) {
  if ((data == nullptr && size != 0) || !ensure_size(memory, offset, size)) {
    return;
  }
  const auto* source = static_cast<const std::uint8_t*>(data);
  std::copy_n(source, size,
              memory.begin() + static_cast<std::ptrdiff_t>(offset));
}

void write_cstring(std::vector<std::uint8_t>& memory, uintptr_t offset,
                   const std::string& value, std::size_t max_len) {
  if (max_len == 0 || !ensure_size(memory, offset, max_len)) return;
  auto destination = memory.begin() + static_cast<std::ptrdiff_t>(offset);
  std::fill_n(destination, max_len, std::uint8_t{0});
  const std::size_t n = std::min(value.size(), max_len - 1);
  std::copy_n(value.begin(), n, destination);
}

uintptr_t chunk_address(uint32_t index) {
  return kEntityChunkArea +
         static_cast<uintptr_t>((index & ENTITY_HANDLE_INDEX_MASK) >>
                                ENTITY_CHUNK_SHIFT) *
             kEntityChunkSize;
}

struct MapPreset {
  const char* name;
  Vector3 map_texture_position;
  float map_texture_scale_stored;
  Vector3 bomb_a;
  Vector3 bomb_b;
  Vector3 spawns[10];
};

const MapPreset& map_preset(const char* map_name) {
  static const MapPreset kPresets[] = {
      {
          "de_dust2",
          {-2476.f, 2440.f, 0.f},
          0.003f,
          {-400.f, 2000.f, 16.f},
          {300.f, -1800.f, 16.f},
          {
              {-500, 0, 16},
              {-450, -200, 16},
              {-400, 200, 16},
              {-550, -400, 16},
              {-350, 400, 16},
              {450, 0, 16},
              {500, -200, 16},
              {400, 200, 16},
              {550, -400, 16},
              {350, 400, 16},
          },
      },
      {
          "de_mirage",
          {-3230.f, 1713.f, 0.f},
          0.0028f,
          {-1200.f, 500.f, -100.f},
          {800.f, -600.f, -200.f},
          {
              {-1400, 200, -100},
              {-1300, 0, -100},
              {-1500, 400, -100},
              {-1200, -200, -100},
              {-1600, 100, -100},
              {1100, -300, -200},
              {1200, -100, -200},
              {1000, -500, -200},
              {1300, 0, -200},
              {900, -200, -200},
          },
      },
      {
          "de_inferno",
          {-2087.f, 3870.f, 0.f},
          0.0032f,
          {2080.f, 100.f, 160.f},
          {-300.f, 2800.f, 160.f},
          {
              {2000, -100, 160},
              {1900, 100, 160},
              {2100, -300, 160},
              {1800, 0, 160},
              {2200, 200, 160},
              {-400, 2700, 160},
              {-300, 2900, 160},
              {-500, 2500, 160},
              {-200, 2800, 160},
              {-600, 2600, 160},
          },
      },
  };
  if (map_name) {
    for (const auto& preset : kPresets) {
      if (std::strcmp(map_name, preset.name) == 0) return preset;
    }
  }
  return kPresets[0];
}

}  // namespace

const FieldOffsets& FieldOffsets::get() {
  static FieldOffsets offsets = [] {
    FieldOffsets o;
    apply_schema_to_field_offsets(o);
    return o;
  }();
  return offsets;
}

const HudRadarOffsets& HudRadarOffsets::get() {
  static const HudRadarOffsets offsets;
  return offsets;
}

const ModuleGlobals& ModuleGlobals::get() {
  static const ModuleGlobals globals;
  return globals;
}

Vector3 default_bomb_origin(const char* map_name) {
  return map_preset(map_name).bomb_a;
}

Vector3 default_map_texture_position(const char* map_name) {
  return map_preset(map_name).map_texture_position;
}

SimCs2Arena make_cs2_arena(int player_count, const char* map_name) {
  SimCs2Arena arena;
  arena.map_name = map_name ? map_name : "de_dust2";
  arena.world = sim::make_arena(OBF("cs2.exe"));
  arena.game_pid = arena.world.game_pid();

  const MapPreset& preset = map_preset(arena.map_name.c_str());
  std::vector<PlayerData> players;
  const int count = std::clamp(player_count, 0, 10);

  for (int i = 0; i < count; ++i) {
    PlayerData player;
    player.controller_index = static_cast<uint32_t>(i + 1);
    player.pawn_index = static_cast<uint32_t>(i + 65);
    player.controller_handle = player.controller_index;
    player.pawn_handle = player.pawn_index;
    player.health = 100;
    player.armor = (i % 2 == 0) ? 100 : 0;
    player.team = i < 5 ? Team_CT : Team_T;
    player.life_state = LifeState_Alive;
    player.is_alive = true;
    player.origin = preset.spawns[i];
    player.old_origin = preset.spawns[i];
    player.eye_angles = {0.0f, static_cast<float>(i * 20), 0.0f};
    // Spotting mask is written after the loop once the local pawn index is
    // known (player 0 is local): bit N set means spotted by pawn N, so the
    // HudEcho renderer sees exactly what the official radar would show.
    player.spotted_mask = 0;
    player.is_local = (i == 0);
    player.desired_fov = 90;
    player.name = "player" + std::to_string(i + 1);
    player.weapon_name = (player.team == Team_CT) ? "weapon_m4a1" : "weapon_ak47";
    player.active_weapon_handle = 200u + static_cast<uint32_t>(i);
    players.push_back(std::move(player));
  }

  if (!players.empty()) {
    arena.local_player_id = players.front().pawn_index;
    // Team spotting residual: bit N set means "spotted by pawn N". Local player
    // 0 is CT, so teammates and the first two Ts are visible to the local team,
    // while the remaining Ts stay unspotted (HudEcho hides them like the
    // official radar does).
    const uint64_t local_bit = 1ULL << (players.front().pawn_index & 63);
    for (std::size_t i = 0; i < players.size(); ++i) {
      if (players[i].is_local) continue;
      const bool visible_to_local_team =
          players[i].team == players.front().team || i <= 6;
      if (visible_to_local_team) players[i].spotted_mask = local_bit;
    }
  }
  arena.player_ids.reserve(players.size());
  for (const auto& player : players) {
    arena.player_ids.push_back(player.pawn_index);
  }

  arena.bomb.planted = true;
  arena.bomb.ticking = true;
  arena.bomb.defused = false;
  arena.bomb.origin = preset.bomb_a;
  arena.bomb.timer = 40.0f;
  arena.bomb.defuse_countdown = 0.0f;
  arena.bomb.site = 0;
  arena.bomb.site_name = "A";
  arena.bomb.entity_index = 128;

  plant_cs2_entity_data(arena.world, arena.game_pid, players);
  plant_cs2_hud_data(arena.world, arena.game_pid, arena.map_name.c_str());
  plant_cs2_bomb_data(arena.world, arena.game_pid, arena.bomb);

  ViewMatrix identity;
  identity.valid = true;
  for (int i = 0; i < 4; ++i) identity.m[i][i] = 1.f;
  plant_cs2_view_matrix(arena.world, arena.game_pid, identity);
  plant_cs2_diagnostic_baseline(arena.world);
  return arena;
}

void plant_cs2_entity_data(sim::World& world, std::uint32_t game_pid,
                           const std::vector<PlayerData>& players) {
  auto* game = world.proc(game_pid);
  if (!game) return;

  auto& memory = game->memory;
  if (memory.size() < kGameMemorySize) memory.assign(kGameMemorySize, 0);
  const auto& fields = FieldOffsets::get();

  // Source 2's outer table starts at entity_list_base + 0x10. Every slot
  // addresses a 512-entry EntityIdentity chunk with ENTITY_IDENTITY_STRIDE
  // byte records.
  for (uint32_t outer = 0; outer < 64; ++outer) {
    const uintptr_t chunk = kEntityChunkArea + outer * kEntityChunkSize;
    write_ptr(memory, SimLayout::ENTITY_LIST + ENTITY_CHUNK_TABLE_OFFSET +
                          outer * 8,
              chunk);
  }

  // Global pointers used by local-player resolution.
  write_ptr(memory, SimLayout::GLOBALS_AREA + 0x00, SimLayout::ENTITY_LIST);
  if (!players.empty()) {
    const auto& local = players.front();
    const uintptr_t local_pawn = SimLayout::PAWN_AREA +
                                 local.pawn_index * SimLayout::PAWN_STRIDE;
    const uintptr_t local_controller =
        SimLayout::CONTROLLER_AREA +
        local.controller_index * SimLayout::CONTROLLER_STRIDE;
    write_ptr(memory, SimLayout::GLOBALS_AREA + 0x08, local_pawn);
    write_ptr(memory, SimLayout::GLOBALS_AREA + 0x10, local_controller);
  }

  for (const auto& player : players) {
    const uintptr_t controller = SimLayout::CONTROLLER_AREA +
                                 player.controller_index *
                                     SimLayout::CONTROLLER_STRIDE;
    const uintptr_t pawn =
        SimLayout::PAWN_AREA + player.pawn_index * SimLayout::PAWN_STRIDE;
    const uintptr_t scene_node = SimLayout::SCENE_NODE_AREA +
                                 player.pawn_index * SimLayout::SCENE_NODE_STRIDE;
    const uint32_t pawn_handle =
        player.pawn_handle ? player.pawn_handle
                           : (player.pawn_index & ENTITY_HANDLE_INDEX_MASK);

    write_int(memory, controller + fields.controller_pawn_handle,
              static_cast<int>(pawn_handle));
    write_cstring(memory, controller + fields.controller_name, player.name,
                  kMaxPlayerName);
    write_u8(memory, controller + fields.controller_is_local,
             player.is_local ? 1 : 0);
    write_int(memory, controller + fields.desired_fov, player.desired_fov);

    write_ptr(memory, pawn + fields.scene_node, scene_node);
    write_int(memory, pawn + fields.health, player.health);
    write_int(memory, pawn + fields.team, player.team);
    write_int(memory, pawn + fields.life_state, player.life_state);
    write_int(memory, pawn + fields.armor, player.armor);
    write_float(memory, pawn + fields.eye_angles, player.eye_angles.pitch);
    write_float(memory, pawn + fields.eye_angles + 4, player.eye_angles.yaw);
    write_float(memory, pawn + fields.eye_angles + 8, player.eye_angles.roll);
    write_ptr(memory, pawn + fields.spotted_mask, player.spotted_mask);
    write_u8(memory, pawn + fields.is_scoped, player.is_scoped ? 1 : 0);
    write_u8(memory, pawn + fields.is_defusing, player.is_defusing ? 1 : 0);
    write_float(memory, pawn + fields.flash_duration, player.flash_duration);
    write_vec3(memory, pawn + fields.old_origin, player.old_origin.x != 0 ||
                                                         player.old_origin.y != 0 ||
                                                         player.old_origin.z != 0
                                                     ? player.old_origin
                                                     : player.origin);
    // Weapon services pointer lives at pawn+offset and points into the same
    // pawn block for the educational layout.
    const uintptr_t weapon_services = pawn + 0x3000;
    write_ptr(memory, pawn + fields.weapon_services, weapon_services);
    write_int(memory, weapon_services + fields.active_weapon,
              static_cast<int>(player.active_weapon_handle));

    write_vec3(memory, scene_node + fields.node_origin, player.origin);

    const uintptr_t controller_identity = identity_slot_addr(
        chunk_address(player.controller_index), player.controller_index);
    const uintptr_t pawn_identity =
        identity_slot_addr(chunk_address(player.pawn_index), player.pawn_index);
    write_ptr(memory, controller_identity + 0x08, controller);
    write_ptr(memory, pawn_identity + 0x08, pawn);
  }
}

void plant_cs2_hud_data(sim::World& world, std::uint32_t game_pid,
                        const char* map_name) {
  auto* game = world.proc(game_pid);
  if (!game) return;

  auto& memory = game->memory;
  ensure_size(memory, kGameMemorySize);
  const uintptr_t hud = SimLayout::HUD_RADAR;
  const auto& offsets = HudRadarOffsets::get();
  const MapPreset& preset = map_preset(map_name);
  write_int(memory, hud + offsets.is_round, 1);
  write_vec3(memory, hud + offsets.map_texture_position,
             preset.map_texture_position);
  write_float(memory, hud + offsets.visibility_size_max, 350.0f);
  write_float(memory, hud + offsets.visibility_size, 300.0f);
  write_float(memory, hud + offsets.map_texture_scale,
              preset.map_texture_scale_stored);
  write_float(memory, hud + offsets.max_visibility_sq, 25000000.0f);
  write_vec3(memory, hud + offsets.origin_tex_diff, {0, 0, 0});
  write_float(memory, hud + offsets.radar_scale, 0.7f);
  write_u8(memory, hud + offsets.is_square, 0);
}

void plant_cs2_bomb_data(sim::World& world, std::uint32_t game_pid,
                         const BombData& bomb) {
  auto* game = world.proc(game_pid);
  if (!game) return;

  auto& memory = game->memory;
  ensure_size(memory, kGameMemorySize);
  const auto& fields = FieldOffsets::get();
  const uintptr_t bomb_entity = SimLayout::BOMB_AREA;
  const uint32_t bomb_index =
      bomb.entity_index ? bomb.entity_index : 128u;

  write_u8(memory, bomb_entity + fields.bomb_ticking,
           (bomb.planted && bomb.ticking && !bomb.defused) ? 1 : 0);
  write_int(memory, bomb_entity + fields.bomb_site, bomb.site);
  write_float(memory, bomb_entity + fields.bomb_blow, bomb.timer);
  write_float(memory, bomb_entity + fields.bomb_defuse_count,
              bomb.defuse_countdown);
  write_u8(memory, bomb_entity + fields.bomb_defused, bomb.defused ? 1 : 0);
  write_vec3(memory, bomb_entity + fields.old_origin, bomb.origin);

  // Publish planted C4 pointer in globals area.
  write_ptr(memory, SimLayout::GLOBALS_AREA + 0x18, bomb.planted ? bomb_entity : 0);

  // Register bomb entity in the entity list chunk so resolve can walk it.
  const uintptr_t bomb_identity =
      identity_slot_addr(chunk_address(bomb_index), bomb_index);
  write_ptr(memory, bomb_identity + 0x08, bomb_entity);
  write_ptr(memory, list_entry_addr(SimLayout::ENTITY_LIST, bomb_index),
            chunk_address(bomb_index));
}

void plant_cs2_view_matrix(sim::World& world, std::uint32_t game_pid,
                           const ViewMatrix& matrix) {
  auto* game = world.proc(game_pid);
  if (!game) return;
  auto& memory = game->memory;
  ensure_size(memory, kGameMemorySize);
  write_bytes(memory, SimLayout::VIEW_MATRIX_AREA, matrix.m, sizeof(matrix.m));
  write_u8(memory, SimLayout::VIEW_MATRIX_AREA + sizeof(matrix.m),
           matrix.valid ? 1 : 0);
}

void plant_cs2_diagnostic_baseline(sim::World& world) {
  seed_diagnostic_fixtures(world, /*include_cheat_scars=*/false);
}

void seed_diagnostic_fixtures(sim::World& world, bool include_cheat_scars) {
  auto& state = world.diagnostic_state;
  state.convars.clear();
  state.vmts.clear();
  state.call_records.clear();
  state.message_records.clear();
  state.event_listeners.clear();
  state.counter_strafe_events.clear();
  state.exceptions.clear();

  state.convars.push_back({"sv_cheats", "0", true, false, 0});
  state.convars.push_back({"cl_radar_scale", "0.7", false, false, 0});
  state.convars.push_back({"cl_drawhud", "1", false, false, 0});
  state.convars.push_back({"mp_damage_scale_ct_head", "1.0", true, false, 0});
  if (include_cheat_scars) {
    state.convars.push_back({"sv_cheats", "1", true, true, 0});
    state.tampered_convar = true;
  }

  state.vmts.push_back({0x140001000, "IClientEntityList", "client.dll", false, true});
  state.vmts.push_back({0x140002000, "IEngineClient", "engine2.dll", false, true});
  state.vmts.push_back({0x140003000, "C_CSPlayerPawn", "client.dll", true, true});
  state.vmts.push_back({0x140003000, "C_CSPlayerPawn", "client.dll", true, true});
  if (include_cheat_scars) {
    state.vmts.push_back({0x7FF00001000, "HookedCreateMove", "cheat.dll", false, false});
    state.anomalous_vmt = true;
  }

  state.event_listeners.push_back(
      {"player_hurt", "client.dll", true, false});
  state.event_listeners.push_back(
      {"round_start", "client.dll", true, false});
  if (include_cheat_scars) {
    state.event_listeners.push_back(
        {"player_hurt", "radar_hook.dll", false, true});
  }

  // Baseline PE timestamps match PETimestampSensor expected values.
  world.pe_timestamp_client_dll = 0x66800000;
  world.pe_timestamp_kernel32 = 0x66000000;
  world.pe_timestamp_cs2_exe = 0x667F0000;
  world.pe_timestamp_ntdll = 0x65FF0000;
  world.pe_timestamp_gameoverlay = 0x667E0000;
  state.expected_pe_timestamp_client_dll = 0x66800000;
  state.pe_timestamp_client_dll = 0x66800000;
  state.is_active = true;
  state.has_focus = true;
  state.b_secure_allowed = !include_cheat_scars;
  world.diagnostic_system_active = true;
  world.client_allowed_on_secure = !include_cheat_scars;
  world.client_allowed_on_secure_servers = !include_cheat_scars;
}

}  // namespace cs2
