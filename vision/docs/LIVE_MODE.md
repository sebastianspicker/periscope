# Live mode

Command: `cs2-vision live`.

Captures frames from screen, a capture device, or a file, runs a segmenter, and draws outlines. Optional transparent overlay (Win32 layered window on Windows, Cocoa when available on macOS, tkinter fallback).

Second-device composite (HDMI capture card on another machine, contours shown fullscreen): [CONTOUR_PIPELINE.md](CONTOUR_PIPELINE.md).

Desktop alternative: `cs2-vision gui` — same engine, tkinter dashboard. See [CLI.md](CLI.md).

## Prerequisites

- Installed package (`uv sync`)
- ONNX model + matching `.model.json` manifest (for backends that require one)
- Screen capture: OS permission (macOS Screen Recording)
- Overlay over CS2 on Windows: borderless windowed display mode

## Quick start

```bash
uv run cs2-vision download-model yolo26n-seg --output-dir artifacts

uv run cs2-vision live \
  --model artifacts/yolo26n-seg.onnx \
  --manifest artifacts/yolo26n-seg.model.json \
  --class-name person \
  --source-type screen \
  --output-mode alpha \
  --overlay
```

List capture devices:

```bash
uv run cs2-vision live --list-devices
```

## Config file

```bash
cp configs/cs2-vision-config.example.json cs2-vision-config.json
# edit model.path, model.manifest, input.source_type, display options
uv run cs2-vision live --config cs2-vision-config.json
```

`cs2-vision setup --auto` can scaffold a config and download a default model. Root `cs2-vision-config.json` is gitignored. CLI flags override config keys.

## Important flags

| Flag | Meaning |
|------|---------|
| `--source-type` | `screen`, `capture-device`, or `file` |
| `--monitor` | 1-based monitor index for screen capture |
| `--input-device` | Device index, name substring, or file path |
| `--output-mode` | `overlay`, `alpha`, or `green` |
| `--output-stream` | Publish rendered frames to a shared-memory ring (`shm:<name>`) for a second process |
| `--max-capture-age-ms` | Drop capture frames older than this many ms of capture time (0 = off) |
| `--fourcc` | Capture codec hint (`MJPG`, `YUY2`) for capture cards |
| `--backend` | OpenCV capture backend hint (`auto`, `dshow`, `avfoundation`, `v4l2`, `msmf`) |
| `--overlay` | Transparent always-on-top overlay window |
| `--overlay-x` / `--overlay-y` | Overlay window top-left (pixels) |
| `--overlay-monitor` | Overlay monitor index (`0` = auto) |
| `--headless` | No preview window |
| `--segmenter-backend` | Backend name (default `ultralytics-onnx`) |
| `--class-name` | Class filter (e.g. `person` or `player`) |
| `--confidence` | Score threshold |
| `--device` | Inference device (`cpu`, `cuda:0`, …) |
| `--temporal-enabled` | Opt-in suppress-only stability filter |
| `--temporal-min-frames` | Consecutive matches required before draw (default 2 when enabled) |
| `--temporal-hold` | Optional hold-last-mask across short dropouts (off by default) |
| `--temporal-max-dropout` | Max hold frames when hold is on (`0` = no carry) |

Full help: `uv run cs2-vision live --help`.

### Segmenter backends

Default is `ultralytics-onnx`. Other names accepted by `--segmenter-backend`:

| Backend | Role |
|---------|------|
| `ultralytics-onnx` | YOLO-seg ONNX (default) |
| `ultralytics-detect` | Box rectangles only |
| `rfdetr` | Optional RF-DETR (`[rfdetr]` extra) |
| `cs2-sam` | Vombit + EdgeSAM hybrid |
| `yolov10`, `nanodet` | Detect-only ORT backends |

Offline multi-backend timing: [BAKEOFF.md](BAKEOFF.md). Package map: [ARCHITECTURE.md](ARCHITECTURE.md).

### Temporal filter

Default is frame-sync only (policy off). With `--temporal-enabled`, unstable one-frame blips are suppressed until a detection matches for N consecutive frames. No track IDs. Hold-last-mask is a separate opt-in (`--temporal-hold`) and is not the default accessibility path — holding stale geometry risks outlining empty space or occluders. Outline CLI uses the same suppress-only idea under `--temporal-suppress` ([ACCESSIBILITY.md](ACCESSIBILITY.md)).

## Overlay positioning

The overlay can sit anywhere and auto-aligns to the captured region or selected monitor when placement flags are omitted. Persist placement under config keys `display.overlay_x`, `display.overlay_y`, and `display.overlay_monitor`.

## Hotkeys

| Key | Action |
|-----|--------|
| 1 / 2 / 3 | Outline presets |
| + / = | Increase outline width |
| - | Decrease outline width |
| T | Toggle temporal filter |
| O | Cycle output mode |
| F | Toggle fill |
| Space | Pause |
| H | Diagnostics |
| Q / Escape | Quit |

Global hotkeys (`RegisterHotKey`) work in overlay mode on Windows even when the game has focus. Other platforms are best-effort while the preview window has focus. The desktop GUI exposes the same actions as buttons.

## Desktop GUI

```bash
uv run cs2-vision gui
```

Tkinter dashboard with Capture / Model / Style / Overlay-Output / Run tabs. Runs the same `run_live_pipeline` engine in a background thread; overlay window optional. Needs no extra install (tkinter is stdlib). Details: [CLI.md](CLI.md).

## Operation notes

- Frame skipping kicks in when inference is slower than capture so the UI stays interactive.
- Capture reads reconnect with bounded backoff (1 s, 2 s, 4 s) and drain buffered frames after a successful reconnect; a card pulled and replugged mid-session recovers without a restart.
- `--max-capture-age-ms` bounds latency by dropping frames that are already stale. Without it, a slow stage turns into an ever-growing queue.
- `--output-stream shm:<name>` replaces (not adds to) any file `--output-sink`; it publishes the rendered frame into a 48-byte-header RGBA ring with newest-wins drops and torn-read detection. Consumers use `read_shared_frame`.
- Alpha and green modes are for external compositors (e.g. OBS), not for writing into the game process.
- Second-device composite (capture card + fullscreen presenter on another Windows box): [CONTOUR_PIPELINE.md](CONTOUR_PIPELINE.md).
- Prefer a CS2-trained `player` model for product experiments; COCO `person` is a wiring check only.
