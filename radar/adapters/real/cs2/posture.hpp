// posture.hpp — Intel exposure + read-shaping policy (pure, host-testable).
//
// Two independent concerns live here on purpose:
//
//   * BehaviorShaper decides *when* to read. Reads cluster around gameplay
//     events (plant, defuse, round transition, local rotate) and fall back to a
//     quiet floor between them, so the access pattern correlates with the match
//     instead of a wall-clock cadence.
//
//   * DegradeLadder decides *how much* to expose. A rising blue composite
//     pushes the posture down; sustained clean windows relax it slowly.
//
// Ordering matters and is enforced by `apply_posture`: event shaping raises the
// floor, then the ladder caps it. Capping last is what makes a degrade survive
// the next frame — the per-frame shaper alone would overwrite it.
//
// Nothing here touches Windows, the process, or the clock domain beyond what
// the caller passes in, so radar/tests can drive it directly.

#pragma once

#include <chrono>
#include <cstdint>

namespace real::cs2::stack {

// ── Intel exposure ────────────────────────────────────────────────
// HudEcho renders only what the official in-game radar could show: teammates
// always, enemies only while the team spotting mask covers them. That keeps the
// pre-aim / info-advantage residual at zero. Full shows every enemy including
// unspotted ones.
enum class IntelMode : int { HudEcho = 0, Full = 1 };

// Auto lets the degrade ladder move intel; HudEcho and Full pin it. A pinned
// mode is never silently overridden — that is the difference between "stealth
// by default" and "stealth whether or not you asked".
enum class IntelPolicy : int { Auto = 0, HudEcho = 1, Full = 2 };

inline const char* intel_mode_name(IntelMode m) noexcept {
  return m == IntelMode::Full ? "full" : "hud-echo";
}

inline const char* intel_policy_name(IntelPolicy p) noexcept {
  switch (p) {
    case IntelPolicy::HudEcho: return "pinned-hud-echo";
    case IntelPolicy::Full: return "pinned-full";
    default: return "auto";
  }
}

/// Mode a pinned policy forces, or nullopt-like `false` when the ladder decides.
inline bool policy_pins_mode(IntelPolicy p, IntelMode& out) noexcept {
  if (p == IntelPolicy::HudEcho) {
    out = IntelMode::HudEcho;
    return true;
  }
  if (p == IntelPolicy::Full) {
    out = IntelMode::Full;
    return true;
  }
  return false;
}

/// Cycle order for the I key: auto → pinned-hud-echo → pinned-full → auto.
inline IntelPolicy next_intel_policy(IntelPolicy p) noexcept {
  switch (p) {
    case IntelPolicy::Auto: return IntelPolicy::HudEcho;
    case IntelPolicy::HudEcho: return IntelPolicy::Full;
    default: return IntelPolicy::Auto;
  }
}

// ── Read shaping ──────────────────────────────────────────────────
struct ReadRates {
  int full_hz = 8;   // priority 0/1 scans
  int local_hz = 15; // local-player refresh
  int far_hz = 4;    // dormant/far controllers
};

struct RoundEvents {
  bool valid = false;
  bool is_round = false;
  bool bomb_planted = false;
  bool bomb_defusing = false;
};

struct BehaviorShaper {
  bool was_bomb_planted = false;
  bool was_round_live = false;
  bool was_defusing = false;
  std::chrono::steady_clock::time_point surge_until{};

  /// Feed one sample of round state plus local movement magnitude (world units).
  void note_round(const RoundEvents& ev, float local_move_units,
                  std::chrono::steady_clock::time_point now) noexcept {
    bool event = false;
    if (ev.valid) {
      if (ev.bomb_planted && !was_bomb_planted) event = true;
      if (ev.bomb_defusing && !was_defusing) event = true;
      // Round transition (live → not live or the reverse) is a natural moment
      // for a radar to refresh.
      if (was_round_live != ev.is_round) event = true;
      was_bomb_planted = ev.bomb_planted;
      was_defusing = ev.bomb_defusing;
      was_round_live = ev.is_round;
    }
    // A real reposition (rotate, retake) counts; small jitter does not.
    if (local_move_units > 256.f) event = true;
    if (event) surge_until = now + std::chrono::seconds(4);
  }

  bool surging(std::chrono::steady_clock::time_point now) const noexcept {
    return now < surge_until;
  }

  /// Event burst: bounded at 1.5x the floor, never above the historical
  /// defaults, so volume stays aligned with gameplay rather than spiking.
  ReadRates rates_at(std::chrono::steady_clock::time_point now,
                     const ReadRates& floor) const noexcept {
    if (!surging(now)) return floor;
    ReadRates burst;
    burst.full_hz = min_cap(floor.full_hz * 3 / 2, 30);
    burst.local_hz = min_cap(floor.local_hz * 3 / 2, 45);
    burst.far_hz = min_cap(floor.far_hz + 2, 8);
    return burst;
  }

 private:
  static int min_cap(int value, int cap) noexcept { return value < cap ? value : cap; }
};

// ── Silent degrade ────────────────────────────────────────────────
// Level 0 = full posture, level 3 = hud-echo floor with far scans off.
// Rising risk steps down immediately; recovery needs CLEAN_WINDOWS_TO_RELAX
// clean windows per rung so the posture does not oscillate.
struct DegradeLadder {
  static constexpr int kMaxLevel = 3;
  static constexpr int kCleanWindowsToRelax = 3;

  int level = 0;
  int clean_windows = 0;
  int last_composite = 0;

  /// Step from one scoring window to the next and return the capped rates plus
  /// the intel mode the ladder would choose on its own.
  IntelMode step(int composite, const ReadRates& base, ReadRates& out) noexcept {
    last_composite = composite;
    if (composite >= 60) {
      if (level < kMaxLevel) ++level;
      clean_windows = 0;
    } else if (composite >= 40) {
      if (level < 2) level = 2;
      clean_windows = 0;
    } else if (composite <= 15) {
      if (++clean_windows >= kCleanWindowsToRelax && level > 0) {
        --level;
        clean_windows = 0;
      }
    } else {
      clean_windows = 0;
    }
    out = capped(base);
    return level >= 1 ? IntelMode::HudEcho : IntelMode::Full;
  }

  /// Rates for the current level. Level 0 only trims to the historical
  /// defaults; every deeper level tightens further.
  ReadRates capped(ReadRates r) const noexcept {
    switch (level) {
      case 0:
        r.full_hz = min_cap(r.full_hz, 20);
        r.local_hz = min_cap(r.local_hz, 30);
        break;
      case 1:
        r.full_hz = min_cap(r.full_hz, 12);
        r.local_hz = min_cap(r.local_hz, 20);
        r.far_hz = min_cap(r.far_hz, 4);
        break;
      case 2:
        r.full_hz = min_cap(r.full_hz, 8);
        r.local_hz = min_cap(r.local_hz, 12);
        r.far_hz = min_cap(r.far_hz, 2);
        break;
      default:
        r.full_hz = 6;
        r.local_hz = 10;
        r.far_hz = 0;  // far scans off entirely
        break;
    }
    return r;
  }

 private:
  static int min_cap(int value, int cap) noexcept { return value < cap ? value : cap; }
};

// ── Composition ───────────────────────────────────────────────────
struct Posture {
  ReadRates rates{};
  IntelMode mode = IntelMode::HudEcho;
};

/// Resolve this frame's posture. Event shaping raises the floor, the ladder
/// caps it, and a pinned policy wins over the ladder's intel choice.
inline Posture apply_posture(const BehaviorShaper& shaper, const DegradeLadder& ladder,
                             IntelPolicy policy, std::chrono::steady_clock::time_point now,
                             const ReadRates& floor) noexcept {
  Posture p;
  p.rates = shaper.rates_at(now, floor);
  IntelMode ladder_mode = ladder.level >= 1 ? IntelMode::HudEcho : IntelMode::Full;
  p.rates = ladder.capped(p.rates);
  IntelMode pinned{};
  p.mode = policy_pins_mode(policy, pinned) ? pinned : ladder_mode;
  return p;
}

}  // namespace real::cs2::stack
