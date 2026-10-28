# Contributing

This is the **vision** track of the Periscope monorepo (`vision/`). Touch files under `vision/` for package work; monorepo-wide process and templates live at the Periscope root (root `CONTRIBUTING` / `.github/` when present). Sister track: `radar/` (memory-path lab — different license and stack).

Pre-alpha research tooling for CS2 visible-player outlining (offline files, live capture, dataset helpers, training). Match the code under `src/cs2_vision_access/` and keep the CLI surface in sync with `docs/CLI.md` and `README.md`.

## Before you open a change

1. Keep Python 3.11–3.13 compatibility (`requires-python` in `pyproject.toml`).
2. Do not commit local weights, videos, or machine-specific configs (`data/*`, `artifacts/*` except README files, `cs2-vision-config.json` under `vision/`).

## Development setup

From `vision/` (directory with `pyproject.toml` and `uv.lock`):

```bash
uv venv --python 3.12
uv sync --frozen --extra dev
# optional:
uv sync --frozen --extra train
uv sync --frozen --extra gpu
```

## Checks (aligned with CI)

Repo-root CI: monorepo `.github/workflows/vision-ci.yml` (path filters on `vision/**`, runs with `working-directory: vision`). Local checks stay the same — run from `vision/`:

```bash
uv sync --frozen --extra dev
uv run ruff check src/ tests/
uv run ruff format --check src/ tests/
uv run mypy --strict src/cs2_vision_access/
uv run pytest tests/ -q
```

CI installs with `uv sync --frozen --extra dev` (optional-dependencies, not a uv group). Local `ruff format` without `--check` is fine before you commit; CI requires the check form. Matrix: Ubuntu, Python 3.11–3.13 for lint/typecheck, a single 3.12 job for tests.

`vision/tests/` is currently listed in the root `.gitignore`, so the suite only
exists in checkouts that already have it. Do not be surprised by a fresh clone
with no `tests/` directory.

## Layout

Paths below are relative to `vision/` inside the Periscope monorepo.

| Path | Role |
|------|------|
| `src/cs2_vision_access/` | Installable package |
| `configs/*.example.json` | Example live and train-auto configs |
| `docs/` | Active topic docs |
| `scripts/` | Maintenance scripts (for example notebook generator) |
| `../radar/` | Sister Periscope track (out of scope for vision PRs) |
| monorepo `.github/workflows/vision-ci.yml` | CI for this track |

## Style

- Prefer small, tested modules over one-off scripts for library behavior.
- Training helpers that notebooks call should stay importable without interactive UI.
- Session-split datasets assign whole `session_id` values to train/val/test; do not introduce random adjacent-frame splits for product layouts.
- Do not add aim assist, memory access, or input injection paths.

## Pull requests

Use the checklist in monorepo-root `.github/PULL_REQUEST_TEMPLATE.md` when present. Say what changed and how you tested it. Call out breaking CLI or config changes. Keep vision PRs limited to `vision/` unless the change is monorepo wiring (for example CI path filters).

## Reporting issues

Prefer the bug / feature forms under monorepo-root `.github/ISSUE_TEMPLATE/` when present. Include OS, Python version, full command line, and whether GPU extras were installed. Do not attach private gameplay frames or credentials. Vulnerability reports: `SECURITY.md` (this track) and monorepo security notes if published at the root.
