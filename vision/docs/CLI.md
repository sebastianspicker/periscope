# CLI reference

Program: `cs2-vision` (`src/cs2_vision_access/interfaces/cli/parser.py`).

```bash
uv run cs2-vision --help
uv run cs2-vision --version
uv run cs2-vision <command> --help
```

Pre-alpha. Commands cover offline outline, live + GUI, dataset/label work, train / train-auto, bakeoff, eval, and study packs. Prefer `cs2-vision <cmd> --help` for the installed flag set.

Related: [ARCHITECTURE.md](ARCHITECTURE.md) · [LIVE_MODE.md](LIVE_MODE.md) · [CONTOUR_PIPELINE.md](CONTOUR_PIPELINE.md) · [TRAIN_AUTO.md](TRAIN_AUTO.md) · [DATASET.md](DATASET.md) · [EVALUATION.md](EVALUATION.md) · [BAKEOFF.md](BAKEOFF.md) · [ACCESSIBILITY.md](ACCESSIBILITY.md) · [USER_STUDY.md](USER_STUDY.md) · [CONTRACTS.md](CONTRACTS.md) · [CUDA.md](CUDA.md)

## Command index

| Command | Purpose |
|---------|---------|
| `outline` | Render outlines into a recorded video |
| `benchmark` | Measure file inference path without writing frames |
| `outline-presets` | List outline presets as JSON |
| `prefs` | Show/set/reset local outline preferences |
| `extract-frames` | Sample video into an annotation folder |
| `validate-dataset` | Validate YOLO polygon labels |
| `audit-dataset` | Structural integrity and session leakage checks |
| `assemble-dataset` | Copy staging sessions into train/val/test by session_id |
| `import-box-dataset` | Import local YOLO detection layout into staging |
| `boxes-to-masks` | Draft polygons from boxes (never auto-GT) |
| `review-drafts` | Accept/reject/promote draft masks |
| `register-model` | Write checksum manifest for a local ONNX file |
| `train` | Fine-tune YOLO-seg and export ONNX + manifest |
| `download-model` | Download/export model + manifest |
| `setup` | Dependency check, optional model download, config scaffold, 5-frame pipeline test |
| `live` | Real-time capture and outline display |
| `gui` | Desktop tkinter dashboard driving the same live pipeline |
| `eval-masks` | Predictions vs YOLO labels |
| `export-predictions` | ONNX over a split → predictions JSON |
| `eval-negatives` | False positives on empty frames |
| `eval-temporal` | Frame-to-frame instability metrics |
| `eval-comfort` | Clutter and stroke contrast proxies |
| `study-render` | Render study stimulus pack |
| `study-aggregate` | Aggregate ratings JSONL |
| `bakeoff` | Sequential offline backend comparison on one video |
| `train-auto` | Multi-stage train from config file |

## train-auto

```bash
uv run cs2-vision train-auto --config configs/train-auto.example.json
uv run cs2-vision train-auto --config path.json --force
uv run cs2-vision train-auto --config path.json --from-stage train
```

Stages: `ingest`, `prepare_data`, `label`, `validate`, `train`, `export`, `self_train`, `eval`, `report`.

See [TRAIN_AUTO.md](TRAIN_AUTO.md).

## download-model

```bash
uv run cs2-vision download-model --list-models
uv run cs2-vision download-model yolo26n-seg --output-dir artifacts
uv run cs2-vision download-model vombit-yolov10n-fp16 --output-dir artifacts
uv run cs2-vision download-model edgesam-encoder --output-dir artifacts
uv run cs2-vision download-model edgesam-decoder --output-dir artifacts
```

Names at runtime: `--list-models` (Ultralytics + registry teachers).

Ultralytics checkpoints export to ONNX with a SHA-256 manifest. ONNX-direct registry entries without class metadata skip the manifest and print a "no manifest" line (they do not invent a fake `--manifest` recommendation).

## live

Documented in [LIVE_MODE.md](LIVE_MODE.md). Core pattern:

```bash
uv run cs2-vision live \
  --model artifacts/model.onnx \
  --manifest artifacts/model.model.json \
  --class-name player \
  --source-type screen \
  --overlay
```

`--overlay` enables the transparent always-on-top overlay window (Win32 layered window on Windows). Overlay placement:

- `--overlay-x` / `--overlay-y` — overlay window top-left position in pixels
- `--overlay-monitor` — monitor index for the overlay (0 = auto)

When none are given, the overlay auto-aligns to the captured screen region or selected monitor.

Second-device composite (HDMI into another machine, contour fullscreen there): [CONTOUR_PIPELINE.md](CONTOUR_PIPELINE.md). Flags: `--output-stream shm:<name>` (shared-memory ring sink), `--fourcc MJPG` (capture codec), `--max-capture-age-ms N` (latency bound). The fullscreen presenter itself is a library backend (`create_fullscreen_presenter`), not a live flag yet.

`--segmenter-backend` defaults to `ultralytics-onnx`. Other backends: `ultralytics-detect`, `rfdetr`, `cs2-sam`, `yolov10`, `nanodet`. Temporal: `--temporal-enabled` (suppress-only); optional `--temporal-hold` / `--temporal-max-dropout`.

Capture-device input and its hardening flags:

```bash
uv run cs2-vision live \
  --input-device 0 --backend dshow --fourcc MJPG \
  --width 1920 --height 1080 --fps 60 \
  --max-capture-age-ms 40 \
  --model artifacts/model.onnx --manifest artifacts/model.model.json \
  --class-name player --output-mode overlay
```

`--output-stream shm:<name>` publishes rendered frames into a shared-memory ring for a second process instead of writing a file, so a presenter or compositor can consume them without filesystem traffic. Wire format and consumer helper live in `adapters/capture/sinks/shared.py`.

## gui

```bash
uv run cs2-vision gui
```

Desktop tkinter dashboard over the same live engine. Tabs:

| Tab | Controls |
|-----|----------|
| Capture | Screen monitor, capture device, or video file |
| Model | ONNX path, manifest, backend, classes, confidence, device |
| Style | Presets, colors, widths, fill, patterns |
| Overlay/Output | Overlay enable, x/y position, monitor, output mode (overlay/alpha/green), alpha fill, temporal, display scale, output sink |
| Run | Start/Stop, live preview, live diagnostics (FPS / inference ms / predictions), log pane, Save/Load config |

The GUI runs `run_live_pipeline` in a background thread (the same engine as `live`) and reuses `cs2-vision-config.json`. No extra install (tkinter is stdlib). Overlay window optional.

## outline / benchmark

```bash
uv run cs2-vision outline \
  --input path/to/clip.mp4 \
  --output artifacts/outlined.mp4 \
  --model artifacts/model.onnx \
  --manifest artifacts/model.model.json \
  --class-name player

uv run cs2-vision benchmark \
  --input path/to/clip.mp4 \
  --model artifacts/model.onnx \
  --manifest artifacts/model.model.json
```

Style, role catalog (`--role-config`), and opt-in suppress-only temporal (`--temporal-suppress`): [ACCESSIBILITY.md](ACCESSIBILITY.md). Prefer `cs2-vision <cmd> --help` for the installed flag set.

## Dataset assembly

```bash
uv run cs2-vision assemble-dataset \
  --staging-root data/staging \
  --output-root data/cs2_players \
  --train session-a,session-b \
  --val session-c
```

Or `--plan path/to/plan.json` with `train` / `val` / `test` session id lists. Full workflow: [DATASET.md](DATASET.md).

## Evaluation

```bash
uv run cs2-vision export-predictions \
  --model artifacts/model.onnx \
  --manifest artifacts/model.model.json \
  --dataset-root data/cs2_players \
  --split val \
  --output artifacts/predictions.v1.json

uv run cs2-vision eval-masks \
  --predictions artifacts/predictions.v1.json \
  --dataset-root data/cs2_players \
  --split val
```

Also: `eval-negatives`, `eval-temporal`, `eval-comfort`. Schema details: [EVALUATION.md](EVALUATION.md).

## bakeoff

```bash
uv run cs2-vision bakeoff \
  --input path/to/recording.mp4 \
  --backends ultralytics-onnx,rfdetr \
  --model-a path/a.onnx --manifest-a path/a.model.json \
  --model-b path/b.onnx --manifest-b path/b.model.json \
  --output artifacts/bakeoff.json
```

Or `--config bakeoff.json` for N backends. No winner is declared. See [BAKEOFF.md](BAKEOFF.md).

## study

```bash
uv run cs2-vision study-render --study path/to/study.json --output-directory artifacts/study-pack
uv run cs2-vision study-aggregate --ratings artifacts/study-pack/ratings.jsonl --output artifacts/study-pack/aggregate.v1.json
```

Protocol: [USER_STUDY.md](USER_STUDY.md).

## Module CLIs (not all under cs2-vision)

| Module | Role |
|--------|------|
| `uv run python -m cs2_vision_access.training.prepare` | Video/tar/live EdgeSAM+Vombit labeling |
| `uv run python -m cs2_vision_access.training.train` | Alternate train script entry |
| `uv run python -m cs2_vision_access.training.bundle` | Zip flat dataset for cloud upload |
| `uv run python -m cs2_vision_access.training.auto` | Same as `train-auto` |

## Exit codes (train-auto)

| Code | Meaning |
|------|---------|
| 0 | Completed (report may still be `degraded`) |
| 1 | Failure |
| 2 | Human gate blocked |
