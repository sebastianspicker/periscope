# Contributing

Periscope is a monorepo of two independent research trees. Pick the subtree you change and follow that subtree’s rules.

## Which guide applies

| Touch | Follow |
|-------|--------|
| `radar/**` | [`radar/CONTRIBUTING.md`](radar/CONTRIBUTING.md) |
| `vision/**` | [`vision/CONTRIBUTING.md`](vision/CONTRIBUTING.md) |
| Root policy / CI only (`README.md`, `CONTRIBUTING.md`, `SECURITY.md`, `NOTICE.md`, root `.gitignore`, `.github/`) | Keep them accurate for both trees; do not invent shared APIs that do not exist |

## Pull request hygiene

- **One concern per PR.** Do not mix unrelated radar and vision changes unless the change is truly shared infra (for example root docs or ignore rules).
- Say which subtree you touched and what you ran (build flags / test filters).
- Match style in the folder you edit. Do not “fix” the other tree in the same PR.

## Do not commit

- Virtualenvs (`.venv/`, `venv/`)
- Build trees (`radar/build*`, CMake cache, `*.obj` / `*.pdb` / `*.exe` droppings)
- Local model weights, ONNX dumps, Ultralytics `runs/`
- Private frames, gameplay video, machine-local configs (`cs2-vision-config.json`, tokens)
- Local development-tool caches and instruction files

Repository ignore rules should already cover these. If something still shows
up in `git status`, update the relevant rule rather than force-adding it.

## Checks

### Radar

From the repository root:

```bash
python3 scripts/verify.py radar-sim
```

Details: [`radar/docs/BUILD-AND-TEST.md`](radar/docs/BUILD-AND-TEST.md), [`radar/CONTRIBUTING.md`](radar/CONTRIBUTING.md).

### Vision

From `vision/`:

```bash
uv sync --extra dev
uv run ruff check src/ tests/
uv run ruff format --check src/ tests/
uv run mypy --strict src/cs2_vision_access/
uv run pytest tests/ -q
uv build
```

From the repository root, the same complete lane is
`python3 scripts/verify.py vision-cpu`. Run
`python3 scripts/verify.py architecture` after changing boundaries or moving
code.

`vision/tests/` and `radar/tests/` are listed in the root `.gitignore`, so a fresh
clone has neither directory: `pytest tests/` errors in `vision/`, and radar CMake
configure fails. Keep them locally until the ignore rules are revisited.

Details: [`vision/CONTRIBUTING.md`](vision/CONTRIBUTING.md).

## Ground rules (both trees)

- Keep work educational / research / accessibility-oriented. Do not ship ready-to-run cheat product packaging aimed at live competitive play.
- Vision: no process memory access, no input injection, no aim assist.
- Radar: simulation is the default path; real backends stay behind CMake flags and should degrade cleanly when hardware or the game is missing.
