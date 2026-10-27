# Stealth posture (October 2026)

How `live_radar` decides what to read and what to show. The posture is the
product decision that separates a "radar" from a "radar that gets noticed".

Design rule: **the radar should be indistinguishable from the official in-game
radar** unless you deliberately opt out. A radar that shows less than the HUD
manufactures no information advantage, so there is no behavioural tell to
correlate — no pre-aiming at unspotted enemies, no reacting to a player nobody
could see.

## Intel modes

| Mode | What it renders | Info advantage |
|---|---|---|
| `hud-echo` (default) | teammates always; enemies only while the team spotting mask covers them | ~zero |
| `full` | every enemy including unspotted ones | real, and visible in your play |

`hud-echo` reads `EntitySpottedState_t::m_bSpotted`, which is the same bit the
game's own radar consumes. That is why the residual is zero rather than merely
small.

### Policy vs. mode

The mode is not chosen directly. You choose a **policy**:

| Policy | Behaviour |
|---|---|
| `auto` (default) | starts at `full`, lets the degrade ladder drop to `hud-echo` |
| `pinned-hud-echo` | always `hud-echo`; the ladder may still slow reads |
| `pinned-full` | always `full`; the ladder may still slow reads |

A pinned policy is never silently overridden — that is the difference between
"stealth by default" and "stealth whether or not you asked for it".

Controls: `--full` starts pinned to `full`, `--strict-intel` starts pinned to
`hud-echo`. The `I` key cycles `auto` → `pinned-hud-echo` → `pinned-full` →
`auto`, and the active policy is printed to the console.

### Strict intel

`m_bSpotted` is one byte in a moving schema. When the read fails the radar could
either:

- **fail open** (assume spotted) — the radar keeps working, but an unspotted
  enemy can leak through `hud-echo`; or
- **fail closed** (assume not spotted) — the radar hides a player it could see.

`--strict-intel` fails closed. The reader records `spotted_known`, so the choice
is explicit rather than a default buried in a helper.

## Read shaping

Reads cluster around gameplay events instead of running on a wall clock:

| Trigger | Effect |
|---|---|
| Bomb planted / defuse started | 4 s burst |
| Round goes live or ends | 4 s burst |
| Local player displaced > 256 u | 4 s burst |

Bursts are bounded at 1.5× the floor and never above the historical defaults
(30/45/8 Hz). Between events the stack sits at the quiet floor of **8 / 15 / 4
Hz** (full / local / far). All three cadences are jittered — a fixed local
refresh is as recognisable as a fixed full scan once timestamps are correlated.

A `far_hz` of `0` genuinely means off: the deadline is parked, so no far read is
issued at all.

## Degrade ladder

`score_blue_dual` produces a composite risk score every 5 s window. The ladder
reacts without any visible change in the overlay:

| Composite | Level | Posture |
|---|---|---|
| ≥ 60 | +1 rung (max 3) | progressively slower scans; far scans off at level 3 |
| ≥ 40 | floor of 2 | 8 / 12 / 2 Hz |
| ≤ 15 | −1 rung per 3 clean windows | relax one step at a time |

Rising risk steps down immediately; recovery needs three consecutive clean
windows so the posture does not oscillate. The ladder only moves the intel mode
while the policy is `auto`.

### Ordering is load-bearing

`apply_posture` runs the shaper first and the ladder caps second. If the ladder
ran first, the next frame's shaper would restore the full speeds and the degrade
would never survive a single frame. `tests/stack_posture_test.cpp` asserts this
directly, along with burst bounds, floor restore, pin immunity, and the
relaxation cadence.

## Offset pinning

Offsets come from a dump, not from pattern scanning at runtime, and the dump is
pinned by date:

- `data/cs2/*.json`, `adapters/real/cs2/offsets_snapshot.hpp` — generated `2026-10-27`
- `src/simulation/cs2/{offsets,schema}.cpp` — mirrored to the same dump

Refresh with `python3 radar/scripts/update_cs2_signatures.py`, then rebuild. The
generated header records the dump timestamp and build number; `--check` reports
drift without writing.

## What the posture does **not** hide

- A `PROCESS_VM_READ` handle still exists somewhere (UI, donor, or worker). The
  attach ladder exists to move that scar off the UI process, not to erase it.
- The overlay window is a real top-level window on the gaming machine. If that
  matters, run the vision track instead — it never opens the game process.
- Read volume is reduced, not eliminated.

## See also

- [../README.md](../README.md) — build and flags
- [TOOLS.md](TOOLS.md) — key bindings
- [RESIDUAL-RESEARCH.md](RESIDUAL-RESEARCH.md) — VMX / BYOVD / DMA / SMM notes
- [STRATEGY-CATALOG.md](STRATEGY-CATALOG.md) — the pairs this posture composes
