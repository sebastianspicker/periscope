#pragma once
#include <cstddef>
#include <cstdint>

namespace cs2 {

// Schema-resolved field offsets (CS2 retail / a2x cs2-dumper snapshot).
// Values track data/cs2/offsets_snapshot.json and are the single source of
// truth for the educational Source-2 memory plant/read path in this library.
struct FieldOffsets {
  // C_BaseEntity / C_BasePlayerPawn
  int health = 0x34C;           // m_iHealth
  int team = 0x3E7;             // m_iTeamNum
  int life_state = 0x354;       // m_lifeState
  int scene_node = 0x330;       // m_pGameSceneNode
  int armor = 0x1ED0;           // m_ArmorValue (C_CSPlayerPawn)
  int old_origin = 0x14A4;      // m_vOldOrigin (dual origin source)
  // CGameSceneNode
  int node_origin = 0xC8;       // m_vecAbsOrigin
  // C_CSPlayerPawn
  int eye_angles = 0x3600;      // m_angEyeAngles
  int spotted_mask = 0x1E94;    // m_entitySpottedState + EntitySpottedState_t::m_bSpottedByMask
  int is_scoped = 0x1EA0;       // m_bIsScoped
  int is_defusing = 0x1EA2;     // m_bIsDefusing
  int flash_duration = 0x1510;  // m_flFlashDuration (C_CSPlayerPawnBase)
  int weapon_services = 0x12F0; // m_pWeaponServices (C_BasePlayerPawn)
  int active_weapon = 0x60;     // CPlayer_WeaponServices::m_hActiveWeapon
  // CCSPlayerController (base CBasePlayerController layout)
  int controller_pawn_handle = 0x92C;  // m_hPlayerPawn
  int controller_name = 0x6FC;         // m_iszPlayerName (char[128] sim)
  int controller_is_local = 0x780;     // m_bIsLocalPlayerController (sim, past name buffer)
  int desired_fov = 0x794;             // m_iDesiredFOV
  // C_PlantedC4
  int bomb_ticking = 0x1288;     // m_bBombTicking
  int bomb_site = 0x128C;        // m_nBombSite
  int bomb_blow = 0x12B8;        // m_flC4Blow
  int bomb_defuse_count = 0x12D8;// m_flDefuseCountDown
  int bomb_defused = 0x12DC;     // m_bBombDefused

  static const FieldOffsets& get();
};

// HUD radar offsets (CCSGO_HudRadar panel layout — educational model).
struct HudRadarOffsets {
  int is_round = 0x060;
  int map_texture_position = 0x190;
  int visibility_size_max = 0x19C;
  int visibility_size = 0x1A0;
  int map_texture_scale = 0x1B4;
  int max_visibility_sq = 0x1B8;
  int origin_tex_diff = 0x1D0;
  int radar_scale = 0x17F8C;
  int is_square = 0x1DC;        // rectangular vs round radar mode

  static const HudRadarOffsets& get();
};

// Module-relative global RVAs (client.dll) — educational defaults matching
// data/cs2/offsets_snapshot.json. Live attachment stays outside simulation.
struct ModuleGlobals {
  std::uint64_t entity_list = 0x271C8B8;
  std::uint64_t game_entity_system = 0x271C8B8;
  std::uint64_t highest_entity_index = 0x2120;
  std::uint64_t local_player_pawn = 0x25676D8;
  std::uint64_t local_player_controller = 0x253EEF8;
  std::uint64_t view_matrix = 0x256CFE0;
  std::uint64_t view_angles = 0x257DE08;
  std::uint64_t planted_c4 = 0x24CD930;
  std::uint64_t csgo_input = 0x257D780;
  std::uint64_t sensitivity = 0x25649D8;

  static const ModuleGlobals& get();
};

// Simulated memory layout within the educational game process image.
// Addresses are offsets into sim::Process::memory (base-relative).
struct SimLayout {
  static constexpr uintptr_t ENTITY_LIST = 0x1000;
  static constexpr uintptr_t HUD_RADAR = 0x3000;
  static constexpr uintptr_t CONTROLLER_AREA = 0x4000;
  static constexpr uintptr_t PAWN_AREA = 0x8000;
  static constexpr uintptr_t SCENE_NODE_AREA = 0xC000;
  static constexpr uintptr_t BOMB_AREA = 0xE000;
  static constexpr uintptr_t GLOBALS_AREA = 0xF000;
  static constexpr uintptr_t VIEW_MATRIX_AREA = 0xF200;
  static constexpr uintptr_t CONVAR_AREA = 0x10000;
  static constexpr uintptr_t CONTROLLER_STRIDE = 0x800;
  static constexpr uintptr_t PAWN_STRIDE = 0x4000;
  static constexpr uintptr_t SCENE_NODE_STRIDE = 0x100;
  static constexpr uintptr_t BOMB_STRIDE = 0x1000;
};

// Source 2 chunked entity list constants (current retail stride = 0x70).
inline constexpr int ENTITY_IDENTITY_STRIDE = 0x70;
inline constexpr int ENTITIES_PER_INNER = 512;
inline constexpr int MAX_CONTROLLERS = 64;
inline constexpr int MAX_PAWNS = 64;
inline constexpr std::uint32_t ENTITY_HANDLE_INDEX_MASK = 0x7FFF;
inline constexpr std::uint32_t ENTITY_CHUNK_SHIFT = 9u;
inline constexpr uintptr_t ENTITY_CHUNK_TABLE_OFFSET = 0x10;

// Entity list formula from actual CS2 (dwEntityList outer table).
inline uintptr_t list_entry_addr(uintptr_t entity_list_base, uint32_t index) {
  uint32_t idx = index & ENTITY_HANDLE_INDEX_MASK;
  return entity_list_base + 8 * (uintptr_t)(idx >> ENTITY_CHUNK_SHIFT) +
         ENTITY_CHUNK_TABLE_OFFSET;
}

inline uintptr_t identity_slot_addr(uintptr_t chunk_ptr, uint32_t index) {
  return chunk_ptr +
         ENTITY_IDENTITY_STRIDE * (uintptr_t)(index & (ENTITIES_PER_INNER - 1));
}

inline uint32_t entity_handle_index(uint32_t handle) {
  return handle & ENTITY_HANDLE_INDEX_MASK;
}

// CS2 user message types used for anti-cheat data collection.
enum class EBaseUserMessages : std::uint32_t {
  UM_AchievementEvent = 0x65,
  UM_CloseCaption = 0x66,
  UM_RequestUtilAction = 0x9d,
  UM_UtilActionResponse = 0x9e,
  UM_DllStatusResponse = 0x9f,
  UM_RequestInventory = 0xa0,
  UM_InventoryResponse = 0xa1,
  UM_RequestDiagnostic = 0xa2,
  UM_DiagnosticResponse = 0xa3,
  UM_CounterStrafe = 0x181
};

// Diagnostic / inventory message numbers used by the educational orchestrator.
inline constexpr std::uint32_t kMsgDllStatus = 159;
inline constexpr std::uint32_t kMsgUtilAction = 157;
inline constexpr std::uint32_t kMsgInventory = 160;
inline constexpr std::uint32_t kMsgMemoryDump = 162;
inline constexpr std::uint32_t kMsgCounterStrafe = 385;

// Steam overlay DLL used for Present hook hijack technique.
inline constexpr const char* kGameOverlayRendererDll = "GameOverlayRenderer64.dll";
// Known offset of Steam's Present trampoline pointer.
inline constexpr std::uint32_t kSteamPresentPtrOffset = 0x162200;
// Known offset of Steam's ResizeBuffers trampoline pointer.
inline constexpr std::uint32_t kSteamResizeBuffersOffset = 0x162208;

}  // namespace cs2
