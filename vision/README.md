# CS2 Vision Access

Vision track of [Periscope](../README.md) (`periscope/vision`). Sister track: [`../radar/`](../radar/) (memory-path radar lab, MIT). This track is pixels-only: offline video and live screen/device capture. It does not open the game process, read game memory, or inject input.

Pre-alpha research tooling for Counter-Strike 2: segment visible player pixels and draw dual-stroke outlines. Offline file pipelines and a live screen/device path share the same segmenter and renderer stack. The primary live deployment is a second Windows device fed by an HDMI capture card, so the gaming machine runs nothing that touches the process — see [docs/CONTOUR_PIPELINE.md](docs/CONTOUR_PIPELINE.md).

Package: `cs2-vision-access` 0.1.0. CLI: `cs2-vision`. License: AGPL-3.0-only. Python: 3.11–3.13.

## Purpose and scope

Help low-vision accessibility research by outlining visible player models on CS2 footage.

**In scope (implemented):**

- Offline outline and timing (`outline`, `benchmark`, `outline-presets`)
- Live capture and OS overlay (`live`), plus a tkinter desktop dashboard (`gui`)
- Second-device contour composite: capture-card fourcc negotiation, bounded reconnect with stale-frame drain, `--max-capture-age-ms` latency bound, shared-memory frame ring (`--output-stream shm:<name>`), and a 1:1 fullscreen presenter (`docs/CONTOUR_PIPELINE.md`)
- Dataset staging, validation, audit, session-split assembly, box import, draft mask review
- Fine-tune and export of YOLO-seg ONNX models with checksum manifests (`train`)
- Multi-stage local auto-train (`train-auto`) and remote notebook loop (`training/remote_autonomous/`)
- Offline eval suite, study stimulus helpers, and backend bakeoff
- Role-catalog multi-signifier treatments and opt-in temporal suppress-only anti-flash
- Segmenter backends: Ultralytics ONNX seg, `ultralytics-detect` (boxes), optional RF-DETR, Vombit+EdgeSAM (`cs2-sam`) for labeling

**Out of scope:**

- Aim assistance, recoil control, or any simulated game input
- Reading or writing CS2 process memory
- Shipping commercial CS2-10k-trained weights without a separate rights review (CS2-10k is CC BY-NC 4.0)

## Capabilities and limitations

### Capabilities

| Area | Entry points |
|------|----------------|
| Offline outline | `cs2-vision outline`, `benchmark`, `outline-presets` |
| Live overlay | `cs2-vision live`, `setup` |
| Second-device composite | `live --source-type capture-device --output-stream shm:<name>`, `--fourcc`, `--max-capture-age-ms` |
| Desktop GUI | `cs2-vision gui` |
| Capture | Screen (`mss`), OpenCV capture device (fourcc negotiation, bounded reconnect), local video file |
| Segmenters | Ultralytics ONNX seg/detect, optional RF-DETR (`rfdetr` extra), Vombit+EdgeSAM (`cs2-sam`) |
| Dataset | `extract-frames`, `validate-dataset`, `audit-dataset`, `assemble-dataset`, box import, draft review |
| Train | `cs2-vision train`, `train-auto`, modules under `training/` |
| Remote research train | Colab/Kaggle notebooks under `src/cs2_vision_access/workflows/training/notebooks/` |
| Eval | `eval-masks`, `export-predictions`, `eval-negatives`, `eval-temporal`, `eval-comfort` |
| Study | `study-render`, `study-aggregate` |
| Compare backends | `bakeoff` |

### Limitations

- Pre-alpha. Public CI runs lint, type checks, unit tests (Python 3.12) and a package build (Python 3.11–3.13). It does not run multi-epoch Ultralytics training or validate live overlay hardware, capture cards, or the fullscreen presenter.
- The fullscreen presenter is Windows-only (`Win32FullscreenPresenter`); the same live pipeline still runs on other hosts in overlay/alpha/green mode.
- COCO `person` models are plumbing checks, not CS2-validated player models.
- Session-split product training needs your labels (or EdgeSAM/prepare drafts) and whole-session train/val assignment. Random frame splits are rejected for product datasets.
- Free-tier notebooks default to COCO-person bootstrap plus self-train on a small CS2-10k slice. EdgeSAM auto-label is opt-in (`USE_EDGESAM=True`) and pulls larger ONNX teachers.
- Live overlay on Windows expects borderless windowed CS2. Anti-cheat policy is outside this software; the code path is external capture only.
- Global hotkeys in overlay mode are implemented on Windows (`RegisterHotKey`); other platforms are best-effort while the preview window has focus. The GUI drives the same controls cross-platform via buttons.
- FPS depends on hardware, resolution, model, and device. Measure with `benchmark` or live diagnostics on your machine.

## Requirements

- Python 3.11, 3.12, or 3.13 (`requires-python = ">=3.11,<3.14"`)
- [uv](https://docs.astral.sh/uv/) recommended for install
- Optional: NVIDIA GPU + CUDA for faster train/live inference (`gpu` extra)
- Optional: `train` extra for WebDataset, Hugging Face hub, Gradio Space
- Optional: `rfdetr` extra for RF-DETR segmenter backend
- Disk space for models under `artifacts/` (gitignored except `artifacts/README.md`)

## Installation

Clone the Periscope monorepo, then work in `vision/`:

```bash
git clone <periscope-url>
cd vision   # package root: pyproject.toml, src/, uv.lock

uv venv --python 3.12
uv sync --frozen --extra dev

# Training / remote data helpers
uv sync --frozen --extra dev --extra train

# GPU ORT + CuPy (CUDA 12 wheel name; change extra for CUDA 11 if needed)
uv sync --frozen --extra gpu
```

`cs2-vision setup` downloads and exports a baseline model with Ultralytics. It
never installs packages at runtime; provision the locked training environment
first when that dependency is absent:

```bash
uv sync --frozen --extra train
```

Editable install comes from the `src/` layout. Console entry point:

```bash
uv run cs2-vision --help
uv run cs2-vision --version
```

## Configuration

### Live config

Copy the template and edit paths (paths relative to `vision/` unless absolute):

```bash
cp configs/cs2-vision-config.example.json cs2-vision-config.json
```

`cs2-vision-config.json` under `vision/` is gitignored for machine-local paths. CLI flags override file values.

```bash
uv run cs2-vision live --config cs2-vision-config.json
# or after setup:
uv run cs2-vision setup --auto
```

### Train-auto configs

| File | Mode |
|------|------|
| `configs/train-auto.example.json` | `flat_cloud` research path |
| `configs/train-auto.session-split.example.json` | `session_split` product-style tree |
| `configs/train-auto.edgesam.example.json` | EdgeSAM label teacher |
| `configs/train-auto.self-train.example.json` | post-export self-train |
| `configs/train-auto.unattended.example.json` | multi-iter unattended |

Stages (see `training/auto/state.py`):

```text
ingest → prepare_data → label → validate → train → export → self_train → eval → report
```

### Environment variables (common)

| Variable | Used by |
|----------|---------|
| `CS2_VISION_REPO` | Notebooks: pip install source for this package |
| `CS2_VISION_GIT` | Notebooks: git clone URL if import fails |
| `CS2_DATASET_ZIP` | Notebooks / remote zip discovery |
| `ALLOW_LEAKY_VAL` | Notebooks: set `1`/`true` to force flat train=val (research only) |
| `CI` | CI sets this |

## Usage

### Download a plumbing model

```bash
uv run cs2-vision download-model --list-models
uv run cs2-vision download-model yolo26n-seg --output-dir artifacts
```

Listed models include `yolo26n-seg` / `yolo26s-seg` / `yolo26m-seg` (the default fine-tune base is `yolo26n-seg`) and `yolo11n-seg`. Paths below stay on the plumbing model for brevity; swap in a CS2-trained model for real work.

### Offline outline

```bash
uv run cs2-vision outline \
  --input path/to/clip.mp4 \
  --output artifacts/outlined.mp4 \
  --model artifacts/yolo11n-seg.onnx \
  --manifest artifacts/yolo11n-seg.model.json \
  --class-name person
```

### Live screen + overlay

```bash
uv run cs2-vision live \
  --model artifacts/yolo11n-seg.onnx \
  --manifest artifacts/yolo11n-seg.model.json \
  --class-name person \
  --source-type screen \
  --output-mode alpha \
  --overlay
```

Windows: use borderless windowed CS2. macOS: grant Screen Recording if prompted.

Overlay position: `--overlay-x`, `--overlay-y`, and `--overlay-monitor` (0 = auto). When unset, the overlay aligns to the captured screen region or selected monitor.

### Second-device composite (capture card)

The gaming machine runs nothing here: its HDMI output goes through a splitter into a capture card on a second Windows box, which segments, composites, and presents fullscreen.

```bash
uv run cs2-vision live \
  --model artifacts/yolo26n-seg.onnx \
  --manifest artifacts/yolo26n-seg.model.json \
  --class-name player \
  --source-type capture-device --input-device 0 \
  --fourcc MJPG --width 1920 --height 1080 --fps 60 \
  --output-mode overlay \
  --output-stream shm:contours \
  --max-capture-age-ms 40
```

Presenter geometry is 1:1 (never scaled); hardware, latency budget, and the presenter contract are in [docs/CONTOUR_PIPELINE.md](docs/CONTOUR_PIPELINE.md).

### Desktop GUI

```bash
uv run cs2-vision gui
```

Tkinter dashboard (stdlib; no extra install) with tabs for Capture, Model, Style, Overlay/Output, and Run. It calls the same `run_live_pipeline` engine as `cs2-vision live` in a background thread. Transparent overlay is optional.

### Dataset: extract, assemble, validate

```bash
uv run cs2-vision extract-frames --video data/clip.mp4 --output data/staging/session-001

uv run cs2-vision assemble-dataset \
  --staging-root data/staging \
  --output-root data/cs2_players \
  --train session-001,session-002 \
  --val session-003

uv run cs2-vision validate-dataset --root data/cs2_players --class-count 1
uv run cs2-vision audit-dataset --root data/cs2_players --class-count 1
```

### Label drafts with EdgeSAM (library/CLI prepare)

```bash
uv run cs2-vision download-model vombit-yolov10n-fp16 --output-dir artifacts
uv run cs2-vision download-model edgesam-encoder --output-dir artifacts
uv run cs2-vision download-model edgesam-decoder --output-dir artifacts

uv run python -m cs2_vision_access.training.prepare \
  --video data/gameplay.mp4 \
  --detector artifacts/yolov10n_cs2_fp16.onnx \
  --manifest artifacts/vombit-yolov10n-fp16.model.json \
  --encoder artifacts/edge_sam_3x_encoder.onnx \
  --decoder artifacts/edge_sam_3x_decoder.onnx \
  --output data/staging \
  --session-id session-001 \
  --collapse-to-player \
  --keep-negatives
```

Exact ONNX filenames depend on `download-model` output; list `artifacts/` after download.

### train-auto (local multi-stage)

```bash
uv run cs2-vision train-auto --config configs/train-auto.session-split.example.json
uv run cs2-vision train-auto --config configs/train-auto.edgesam.example.json
uv run cs2-vision train-auto --config path/to/config.json --force
uv run cs2-vision train-auto --config path/to/config.json --from-stage train
```

### Remote notebooks (research)

- `src/cs2_vision_access/workflows/training/notebooks/colab.ipynb`
- `src/cs2_vision_access/workflows/training/notebooks/kaggle.ipynb`
- Operator notes: `src/cs2_vision_access/workflows/training/notebooks/README.md`

Default: CS2-10k sample + COCO-person bootstrap + iterative self-train. Optional `USE_EDGESAM=True` for Vombit+EdgeSAM auto-label. Regenerate notebooks from:

```bash
uv run python scripts/_gen_remote_notebooks.py
```

### Evaluation

```bash
uv run cs2-vision export-predictions \
  --model path/to/model.onnx \
  --manifest path/to/model.model.json \
  --dataset-root data/cs2_players \
  --split val \
  --output artifacts/predictions.v1.json

uv run cs2-vision eval-masks \
  --predictions artifacts/predictions.v1.json \
  --dataset-root data/cs2_players \
  --split val
```

### Full CLI surface

```text
outline, benchmark, outline-presets, prefs,
extract-frames, validate-dataset, audit-dataset, assemble-dataset,
import-box-dataset, boxes-to-masks, review-drafts,
register-model, train, download-model, setup, live, gui,
eval-masks, export-predictions, eval-negatives, eval-temporal, eval-comfort,
study-render, study-aggregate, bakeoff, train-auto
```

Details: [docs/CLI.md](docs/CLI.md).

## Repository structure

This tree is the `vision/` package root inside the Periscope monorepo (sibling of `radar/`).

```text
vision/   # cwd for uv and cs2-vision
├── configs/                 # Example train-auto and live configs
├── data/README.md           # Local data root (contents gitignored)
├── artifacts/README.md      # Local model/run outputs (contents gitignored)
├── docs/                    # Topic docs (see below)
├── scripts/                 # Notebook generator
├── src/cs2_vision_access/   # Installable package
│   ├── domain/              # Values, schemas, and deterministic policy
│   ├── application/         # Use cases and volatile-boundary ports
│   ├── workflows/           # Dataset, training, evaluation, and study flows
│   ├── adapters/            # Capture, models, rendering, persistence, overlays
│   ├── interfaces/          # CLI and GUI composition edges
│   └── <public facades>/    # Thin compatibility imports only
```

Topic docs:

| Doc | Topic |
|-----|--------|
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Package map |
| [docs/CLI.md](docs/CLI.md) | Command reference |
| [docs/LIVE_MODE.md](docs/LIVE_MODE.md) | Live capture and overlay |
| [docs/CONTOUR_PIPELINE.md](docs/CONTOUR_PIPELINE.md) | Second-device contour composite |
| [docs/TRAIN_AUTO.md](docs/TRAIN_AUTO.md) | Multi-stage auto-train |
| [docs/DATASET.md](docs/DATASET.md) | Dataset policy and layout |
| [docs/EVALUATION.md](docs/EVALUATION.md) | Metrics and eval CLIs |
| [docs/CUDA.md](docs/CUDA.md) | GPU extras |
| [docs/CONTRACTS.md](docs/CONTRACTS.md) | Safety and data contracts |
| [docs/ACCESSIBILITY.md](docs/ACCESSIBILITY.md) | Accessibility research notes |
| [docs/BAKEOFF.md](docs/BAKEOFF.md) | Backend comparison |
| [docs/USER_STUDY.md](docs/USER_STUDY.md) | Study protocol helpers |
| [SECURITY.md](SECURITY.md) | Vulnerability reporting |

## Development workflow

Run these from `vision/` (the directory with `pyproject.toml`):

```bash
uv sync --frozen --extra dev
# code under src/
uv run ruff check src/ tests/
uv run ruff format src/ tests/
uv run mypy --strict src/cs2_vision_access/
uv run pytest tests/ -q
```

CI lives at monorepo root: `.github/workflows/vision-ci.yml` (path filters on `vision/**`, `working-directory: vision`). On `main` push/PR it runs Python 3.11–3.13 ruff and mypy checks, pytest on 3.12, and a package build. No multi-epoch train and no live hardware in CI. See root `.github/` for workflows and issue/PR templates.

`vision/tests/` is currently local-only: the root `.gitignore` ignores it, so a fresh clone has no `tests/` and `uv run pytest tests/` there errors. The suite still runs locally and in CI when the directory is present.

Package metadata: `pyproject.toml`. Lockfile: `uv.lock`.

## Operation

No hosted multi-tenant service here. Typical modes:

1. Local CLI on a research workstation (`uv run cs2-vision …` from `vision/`).
2. Local `train-auto` under `artifacts/auto/<run_id>/` (state, report, models, events).
3. Optional Gradio Space under `src/cs2_vision_access/workflows/training/space/` (`train` extra).
4. Colab/Kaggle notebooks for free-tier iterative train; download ONNX+manifest when done.

Promote ONNX + `.model.json` into a stable path under `artifacts/` for live/outline after training.

## Troubleshooting

| Symptom | Check |
|---------|--------|
| `ModuleNotFoundError: cs2_vision_access` | `uv sync --frozen --extra dev` from `vision/`; run via `uv run` |
| Live import fails for train-only deps | Install `--extra train` only if using those modules |
| ONNX missing | `download-model` or train export; confirm `--manifest` matches file |
| Live no frames / black | Source type, monitor index, capture device index (`--list-devices`) |
| Capture card drops frames / reconnects | `--fourcc MJPG` (or `YUY2`) at the card's native mode; reconnect backs off 1/2/4 s and drains stale buffers automatically |
| Composite drifts behind live | Set `--max-capture-age-ms` (~40 at 1080p60) so late frames are dropped, not queued |
| Presenter shows a size mismatch | Capture resolution must equal the monitor's native resolution; the presenter never scales |
| Overlay not visible on Windows | Borderless windowed CS2; overlay backend Win32 layered window |
| Overlay misaligned / wrong position | Auto-aligns to capture region; use `--overlay-monitor` (0 = auto) or `--overlay-x` / `--overlay-y` |
| GUI won't start | Needs a display; on Linux the tkinter overlay falls back to window alpha |
| train-auto fails validate | Session-split needs `images/train`+`labels/train` (and val); run `validate-dataset` |
| EdgeSAM label stage soft-fails | Models under `artifacts/`; set `label.teacher: edgesam` and optional `label.download_edgesam` |
| Notebook cannot import package | Set `CS2_VISION_REPO` or clone the monorepo and install from `vision/` |
| CUDA OOM during train | Lower batch / imgsz; cloud train can retry with batch halved on OOM |
| Leaky val metrics look optimistic | Do not set `ALLOW_LEAKY_VAL`; use held-out or session-split val |

## Security considerations

- Report vulnerabilities privately when a contact channel is published (see [SECURITY.md](SECURITY.md)).
- Do not attach gameplay frames with personal data, tokens, or private weights to public issues.
- Model manifests bind ONNX files by SHA-256; prefer loading with matching manifests.
- Zip extract paths reject `..` traversal in training cloud helpers.
- Live mode is external capture only; follow platform and anti-cheat terms for your environment.
- The shared-memory ring is local to one machine and has no authentication; any local process that guesses the name can read frames. Treat it as display data, not a secret channel.
- Streamproof mode (`WDA_EXCLUDEFROMCAPTURE`) hides the presenter from device-2 captures; it does not change what the gaming machine can see.
- CS2-10k and other third-party datasets carry their own licenses (for example CC BY-NC 4.0).

## Contribution guidance

See [CONTRIBUTING.md](CONTRIBUTING.md). Monorepo meta (issues across tracks, root templates) is at the Periscope root. Short version for this track:

1. Run ruff/mypy/pytest as in CI (from `vision/`).
2. Do not commit `data/*`, `artifacts/*` weights, or `vision/cs2-vision-config.json`.
3. Prefer session-split datasets for product-oriented training experiments.
4. Keep CLI and config examples consistent with `src/cs2_vision_access/interfaces/cli/parser.py`.

## Model selection notes

- Instance segmentation polygons feed the outline renderer; pure boxes only support rectangular fallbacks (`ultralytics-detect`).
- Ultralytics YOLO26-seg nano (`yolo26n-seg`) is the default fine-tune base in CLI train and cloud helpers; `yolo11n-seg` remains a small plumbing model.
- RF-DETR-Seg is optional (`rfdetr` extra).
- EdgeSAM is a teacher for labeling (Vombit boxes to masks), not the default real-time live backend.
