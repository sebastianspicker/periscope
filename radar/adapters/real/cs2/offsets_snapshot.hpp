#pragma once
// Embedded CS2 offset snapshot (auto-updated by scripts/update_cs2_signatures.py).
// Values are RVAs relative to client.dll base unless noted as field offsets.
// Source: https://raw.githubusercontent.com/a2x/cs2-dumper/main/output/
// Generated: 2026-10-27T19:42:52Z
//
// NOTE: entities.cpp / live_radar_stack.cpp require the full FIELD_MAP set
// (core + armor, dormant, spotted, bomb, observer). emit_hpp emits every key.
//
// PINNED DUMP — late October 2026 (game build 14192). Refresh with
// radar/scripts/update_cs2_signatures.py; `--check` reports drift read-only.
// Never hand-edit values here: the updater regenerates this file wholesale.

#include <cstddef>
#include <cstdint>

namespace real::cs2::snapshot {

inline constexpr const char* kSourceUrl =
    "https://raw.githubusercontent.com/a2x/cs2-dumper/main/output/";
inline constexpr const char* kModule = "client.dll";
inline constexpr const char* kGeneratedAt = "2026-10-27T19:42:52Z";

namespace globals {
inline constexpr std::uint64_t dwEntityList = 0x271C8B8;
inline constexpr std::uint64_t dwGameEntitySystem = 0x271C8B8;
inline constexpr std::uint64_t dwGameEntitySystem_highestEntityIndex = 0x2120;
inline constexpr std::uint64_t dwLocalPlayerPawn = 0x25676D8;
inline constexpr std::uint64_t dwLocalPlayerController = 0x253EEF8;
inline constexpr std::uint64_t dwViewMatrix = 0x256CFE0;
inline constexpr std::uint64_t dwViewAngles = 0x257DE08;
inline constexpr std::uint64_t dwPlantedC4 = 0x24CD930;
inline constexpr std::uint64_t dwCSGOInput = 0x257D780;
inline constexpr std::uint64_t dwSensitivity = 0x25649D8;
}  // namespace globals

namespace fields {
// C_BaseEntity
inline constexpr std::uint64_t m_iHealth = 0x34C;
inline constexpr std::uint64_t m_iTeamNum = 0x3E7;
inline constexpr std::uint64_t m_lifeState = 0x354;
inline constexpr std::uint64_t m_pGameSceneNode = 0x330;
// C_BasePlayerPawn
inline constexpr std::uint64_t m_vOldOrigin = 0x14A4;
// C_CSPlayerPawn
inline constexpr std::uint64_t m_angEyeAngles = 0x3600;
// CCSPlayerController
inline constexpr std::uint64_t m_hPlayerPawn = 0x92C;
// CGameSceneNode
inline constexpr std::uint64_t m_vecAbsOrigin = 0xC8;
inline constexpr std::uint64_t m_bDormant = 0x103;
// C_CSPlayerPawn
inline constexpr std::uint64_t m_ArmorValue = 0x1ED0;
inline constexpr std::uint64_t m_entitySpottedState = 0x1E88;
// EntitySpottedState_t
inline constexpr std::uint64_t m_bSpotted = 0x8;
// C_BasePlayerPawn
inline constexpr std::uint64_t m_pObserverServices = 0x1308;
// CPlayer_ObserverServices
inline constexpr std::uint64_t m_iObserverMode = 0x48;
// C_PlantedC4
inline constexpr std::uint64_t m_bBombTicking = 0x1288;
inline constexpr std::uint64_t m_nBombSite = 0x128C;
inline constexpr std::uint64_t m_flC4Blow = 0x12B8;
inline constexpr std::uint64_t m_bBeingDefused = 0x12C4;
}  // namespace fields

namespace constants {
inline constexpr std::uint64_t entity_identity_stride = 0x70;
inline constexpr std::uint64_t entity_chunk_table_offset = 0x10;
inline constexpr std::uint32_t entity_handle_index_mask = 0x7FFF;
inline constexpr std::uint32_t entity_chunk_shift = 9u;
}  // namespace constants

}  // namespace real::cs2::snapshot
