#pragma once

#include "cs2/entities.hpp"
#include "cs2/radar_math.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace cs2 {

// Intel exposure policy. HudEcho (default) renders only what the game's own
// radar would reveal: teammates always, enemies only while the team spotting
// mask covers them, so the pre-aim / info-advantage residual stays at zero.
// Full is the optional legacy radar mode (all entities, spotted or not).
enum class IntelMode { HudEcho = 0, Full = 1 };

/// True when `flag` appears anywhere in argv (demos accept extra flags).
inline bool has_flag(int argc, char** argv, const char* flag) {
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], flag) == 0) return true;
  }
  return false;
}

inline bool enemy_is_spotted_by(const uint64_t spotted_mask, uint32_t pawn_index) {
  return (spotted_mask & (1ULL << (pawn_index & 63))) != 0;
}

inline void render_radar_console(const char* title,
                                 const std::vector<PlayerData>& players,
                                 const PlayerData* local,
                                 const HudRadarState& hud,
                                 IntelMode mode = IntelMode::HudEcho) {
  constexpr int kRows = 13;
  constexpr int kCols = 29;
  const bool show_all = mode == IntelMode::Full;
  char grid[kRows][kCols + 1];
  for (auto& row : grid) {
    std::fill(row, row + kCols, '.');
    row[kCols] = '\0';
  }

  const int center_row = kRows / 2;
  const int center_col = kCols / 2;
  grid[center_row][center_col] = 'L';
  if (local) {
    for (const auto& player : players) {
      if (player.pawn_index == local->pawn_index ||
          player.life_state != LifeState_Alive) continue;
      const bool teammate = player.team == local->team;
      if (!show_all && !teammate) {
        // HudEcho: an enemy blip only appears while the game's own spotting
        // mask covers the local player — identical to the official radar.
        if (!enemy_is_spotted_by(player.spotted_mask, local->pawn_index)) continue;
      }
      const RadarLayout layout = compute_layout(hud, local->eye_angles.yaw);
      const auto point = translate_to_radar_position(
          hud, player.origin, layout.yaw_radians, layout.radar_to_texture_scale);
      // HUD transforms are map-texture scaled, while visibility_size is in HUD
      // pixels. Cap the console viewport so distinct transformed blips remain
      // visible in this compact textual representation.
      const float range = std::clamp(hud.visibility_size * 0.5f, 1.0f, 5.0f);
      int row = center_row - static_cast<int>(std::lround(
          point.position.y / range * center_row));
      int col = center_col + static_cast<int>(std::lround(
          point.position.x / range * center_col));
      row = std::clamp(row, 0, kRows - 1);
      col = std::clamp(col, 0, kCols - 1);
      grid[row][col] = teammate ? 'C' : 'T';
    }
  }

  std::printf("\n[CS2 HUD Radar] %s (intel=%s)\n",
              title, mode == IntelMode::Full ? "full" : "hud-echo");
  for (const auto& row : grid) std::printf("  %s\n", row);
  std::printf("  L=local yaw=%.1f  C=CT (blue)  T=T (yellow/red)\n",
              local ? local->eye_angles.yaw : 0.0f);
  for (const auto& player : players) {
    std::printf("  %-8s %s hp=%3d pos=(%.0f,%.0f)%s\n", player.name.c_str(),
                player.team == Team_CT ? "CT" : "T ", player.health,
                player.origin.x, player.origin.y,
                (show_all || player.team == (local ? local->team : 0) ||
                 player.pawn_index == (local ? local->pawn_index : 0) ||
                 enemy_is_spotted_by(player.spotted_mask,
                                     local ? local->pawn_index : 0))
                    ? ""
                    : " (hidden: unspotted)");
  }
}

}  // namespace cs2
