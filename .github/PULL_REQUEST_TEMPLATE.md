## Subtree(s)

<!-- Which part of the monorepo does this touch? -->

- [ ] `radar/`
- [ ] `vision/`
- [ ] monorepo meta (root docs, `.github/`, top-level layout)

## Summary

<!-- What changed and why (a few sentences). -->

## Test plan

<!-- Check what you ran. Paths are from the monorepo root. -->

**vision** (from `vision/`):

- [ ] `uv run ruff check src/ tests/`
- [ ] `uv run ruff format --check src/ tests/`
- [ ] `uv run mypy --strict src/cs2_vision_access/`
- [ ] `uv run pytest tests/ -q`
- [ ] `uv build`
- [ ] Extra checks if relevant (live, GPU, train path, notebook regen)

**radar** (from repo root):

- [ ] `python3 scripts/verify.py radar-sim`

**architecture** (from repo root):

- [ ] `python3 scripts/check_architecture.py`

## Safety checklist

- [ ] No private gameplay frames, credentials, or local model weights committed
- [ ] **vision:** remains external-capture / local-video only — no process memory, injection, or simulated game input
- [ ] **radar:** educational / simulation scope only; no live-game weaponization payloads
- [ ] Docs updated if CLI, config, or build behavior changed
- [ ] No breaking public surface change, **or** called out above with migration notes
