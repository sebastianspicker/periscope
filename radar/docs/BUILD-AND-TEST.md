# Build and Test

## Configure and Build

```
cmake -S . -B build -DLR_BUILD_PROTOS=ON -DLR_BUILD_STRATEGY_LAB=ON
cmake --build build -j
```

## Options

| Option | Default | Description |
|--------|:-------:|-------------|
| `LR_BUILD_PROTOS` | ON | Per-tier prototype demos (`proto_tN_*`), CS2 radar demos |
| `LR_BUILD_DUELS` | ON | Red-vs-blue duel demos (`duel_tN`) |
| `LR_BUILD_STRATEGY_LAB` | ON | `strategy_lab` CLI + `ac_strategies` library |
| `LR_ENABLE_REAL_UEFI` | OFF | UEFI firmware analysis backend |
| `LR_ENABLE_REAL_LINUX` | OFF | Linux live backend and portable host stubs |
| `LR_ENABLE_REAL_ALL` | OFF | Enable all real mode backends (Windows only) |

The default configuration is simulation-only on every platform. Real
backends and live-reader features require an explicit CMake opt-in.

See `CMakeLists.txt` for the full list of `LAB_ALLOW_*` flags that enable additional educational simulation surfaces.

## Directory layout

| Kind | Path (from the Radar root) |
|------|------|
| Libraries | `src/domain/ac/`, `src/simulation/sim/`, `src/simulation/ac_sim/`, `src/simulation/cs2/`, `adapters/real/`, … |
| Team libraries | `src/lab_components/teams/t0_red/` … `src/lab_components/teams/t4_blue/` |
| Strategy pairs | `scenarios/strategies/t0/` … `scenarios/strategies/crosscutting/` |
| Demos | `apps/demos/` (flat `.cpp` + `apps/demos/cs2_radar/tN/`) |
| Project docs | `docs/` (curriculum and reference) |

## Test

```bash
ctest --test-dir build --output-on-failure   # portable host tests
./build/strategy_lab all --quiet             # strategy pairs
```

`ctest` covers host-only logic that links no real adapter (currently the intel
and read-shaping posture policy in `tests/stack_posture_test.cpp`). It runs with
the default simulation configuration on every platform.

## Common Binaries

| Binary | Source | Description |
|--------|--------|-------------|
| `strategy_lab` | `scenarios/strategies/framework/main.cpp` | Strategy catalog runner |
| `fps_demo` | `apps/demos/fps_demo.cpp` | FPS game state demo |
| `radar_t0` … `radar_t4` | `apps/demos/radar_t{0-4}.cpp` | Per-tier deterministic simulation demos |
| `live_radar` | `apps/demos/live_radar.cpp` | Opt-in live CS2 radar overlay (real RPM + syscall + GPU) |
| `tier_comparison` | `apps/demos/tier_comparison.cpp` | All-tier timing comparison |
| `full_prototype` | `apps/demos/full_prototype.cpp` | Opt-in real-backend exercise |
| `duel_t0` … `duel_t4` | `apps/demos/duel_t{0-4}.cpp` | Red-vs-blue duels |
| `cs2_radar_t0` … `cs2_radar_t4` | `apps/demos/cs2_radar/t{0-4}/` | Alternative CS2 radars |

Targets backed by `adapters/real/` are created only when their required
capability targets exist. The default target graph contains no real adapter
source.
