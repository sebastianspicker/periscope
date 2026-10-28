# AC Research Lab

Educational red/blue lab for studying **external “legit” radar** — tools that read game memory and draw a map without aiming for you — and the detection techniques that push back.

This is the **radar** track of [Periscope](../README.md) (`periscope/radar`). Sister track: [`../vision/`](../vision/) (pixel-based outlining; separate stack and license). Not a cheat product and not a production anti-cheat.

Two ways to run it:

| Mode | What it does | Needs |
|------|----------------|-------|
| **Simulation** | Red plants scars on a deterministic `sim::World`; blue reasons over them | C++20 + CMake. Any OS. |
| **Real (Windows)** | Attach to a live process (CS2 for T0/T1), read entities, draw a radar overlay | VS/MSVC or Clang-CL, optional WDK / VMX / DMA hardware for higher tiers |

Default builds use `sim::World`. Real Windows backends are optional via CMake.

`live_radar` prefers not to manufacture an information advantage: the default
`auto` policy starts at full intel and lets the degrade ladder fall back to
hud-echo (only what the in-game radar already shows) as blue pressure rises.
Posture and its limits: [docs/STEALTH-POSTURE.md](docs/STEALTH-POSTURE.md).

---

## Why it exists

Commercial “looks legit” radar packs escalate delivery as blue closes lower scars:

```
T0  OpenProcess + ReadProcessMemory
T1  Same handle, indirect / direct syscalls (hooks are theater)
T2  Kernel / BYOVD / memrw device (no game handle)
T3  Personal hypervisor + thin bridge
T4  Off-box DMA / capture residuals
```

Each tier has a red team library, a blue team library, runnable demos, and strategy pair lessons (apply → detect) under `scenarios/strategies/{t0..t4,crosscutting}/`.

---

## Layout

```
.
├── README.md                 You are here
├── CMakeLists.txt            Build entry point
├── cmake/                    CMake helpers and target fragments
├── docs/                     Curriculum and reference
├── src/
│   ├── domain/               Core values, telemetry, server-side policy
│   ├── simulation/           Deterministic world and scenario engines
│   ├── application/          Detection, analysis, strategies, simulation pipeline
│   └── lab_components/       Lab infrastructure and tier components
├── adapters/real/            Optional OS/hardware backends (T0–T4)
├── scenarios/strategies/     Red/blue pair lessons + strategy_lab
├── apps/demos/               radar_tN, live_radar, duels, probes
├── examples/drivers/         Example vulnerable driver (lab only)
├── examples/firmware/        Example PCIe DMA host sim
├── scripts/                  Signature and offset tooling
└── data/                     Offset snapshots and related data
```
---

## Build

### Simulation only (any platform)

```bash
cmake -S . -B build -DLR_BUILD_STRATEGY_LAB=ON
cmake --build build -j
```

### Real backends (Windows)

```bash
cmake -S . -B build -DLR_ENABLE_REAL_ALL=ON
cmake --build build -j
```

Useful CMake flags:

| Flag | Default | Meaning |
|------|:-------:|---------|
| `LR_BUILD_STRATEGY_LAB` | ON | `strategy_lab` CLI |
| `LR_BUILD_DUELS` | ON | `duel_tN` binaries |
| `LR_BUILD_PROTOS` | ON | Prototype demos |
| `LR_ENABLE_REAL_RPM` | OFF | OpenProcess / RPM |
| `LR_ENABLE_REAL_SYSCALL` | OFF | Syscall path |
| `LR_ENABLE_REAL_KERNEL` | OFF | Driver / IOCTL |
| `LR_ENABLE_REAL_VMX` | OFF | VT-x helpers |
| `LR_ENABLE_REAL_DMA` | OFF | DMA / PCIe helpers |
| `LR_ENABLE_REAL_GPU` | OFF | D3D11 overlay path |
| `LR_ENABLE_REAL_UEFI` | OFF | UEFI firmware analysis backend |
| `LR_ENABLE_REAL_LINUX` | OFF | Linux real backend and host stubs |
| `LR_ENABLE_REAL_ALL` | OFF | Flip the real stack on |

All real and live-reader options default to `OFF` on every platform. Use
`LR_ENABLE_REAL_ALL=ON` for the complete supported real stack or enable only
the individual backends needed for an isolated lab exercise.

Runtime knobs: `LR_MODE=sim|real|hybrid`, `LR_VERBOSE=0|1`.

---

## Try it

```bash
# Strategy catalog (simulation)
./build/strategy_lab list
LR_MODE=sim ./build/strategy_lab run 01_external_rpm
./build/strategy_lab all --quiet

# Portable host tests (needs the local, gitignored tests/ directory)
ctest --test-dir build --output-on-failure

# Per-tier deterministic simulation demos
./build/radar_t0
./build/radar_t1

# Windows GPU control panel demo (requires LR_ENABLE_REAL_GPU=ON)
./build/gui_demo

# Opt-in live CS2 radar (real RPM + syscall + GPU), auto posture by default
./build/live_radar
./build/live_radar --full          # pin full intel instead
```

`gui_demo` is created only when the real RPM, syscall, and GPU capabilities
are enabled. It uses simulated entities and does not require a game process.
`INSERT` toggles its control panel; `END` / `ESC` quit. Settings land in
`radar.ini` next to the binary.

`live_radar` defaults to the `auto` policy: it starts at full intel and lets the
degrade ladder drop to hud-echo when blue risk rises, never relaxing back past
hud-echo on its own. `--full` pins all-entity intel, `--strict-intel` pins
hud-echo and fails closed on an unreadable spotted flag. `I` cycles the policy
at runtime.
Posture, read shaping, and the degrade ladder: [docs/STEALTH-POSTURE.md](docs/STEALTH-POSTURE.md).

---

## Docs

| Doc | What it is |
|-----|------------|
| [docs/INDEX.md](docs/INDEX.md) | Full index |
| [docs/OVERVIEW.md](docs/OVERVIEW.md) | Threat model and design takeaways |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Code map and World model |
| [docs/BUILD-AND-TEST.md](docs/BUILD-AND-TEST.md) | Build / test detail |
| [docs/THREAT-TIERS.md](docs/THREAT-TIERS.md) | T0–T4 delivery detail |
| [docs/STRATEGY-CATALOG.md](docs/STRATEGY-CATALOG.md) | Human index of strategy pairs |
| [docs/CURRICULUM.md](docs/CURRICULUM.md) | Suggested learning order |
| [docs/STEALTH-POSTURE.md](docs/STEALTH-POSTURE.md) | Intel policy, read shaping, degrade ladder, offset pinning |

Live catalog truth is always `./build/strategy_lab list` (hundreds of registered pairs under `scenarios/strategies/`).

---

## Safety / ethics

- Example vulnerable driver and DMA firmware are for **controlled lab use**. Do not load them on production machines.
- Real backends exercise OS APIs that commercial anti-cheat watches. Expect detection if you point them at a live game.
- This tree does **not** bypass HVCI/VBS, and it does **not** ship VMT hooks, injectors, or game-memory writers for gameplay advantage. Simulation plants scars on `sim::World`; real mode is read-oriented educational scaffolding.
- See [SECURITY.md](SECURITY.md).

---

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Short version: new lessons go under `scenarios/strategies/<tier>/` with red + blue + `LESSON.md`, register them in `scenarios/strategies/framework/registry.cpp`.

---

## License

MIT — see [LICENSE](LICENSE). Educational use only; you own what you do with it.
