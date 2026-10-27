# Accessibility design

Player models can vanish into busy CS2 scenes. Segmentation alone is not enough — the
outline has to stay visible, configurable, stable, and comfortable.

Related: [LIVE_MODE.md](LIVE_MODE.md) (live + GUI) · [CONTOUR_PIPELINE.md](CONTOUR_PIPELINE.md) (second-device composite) · [USER_STUDY.md](USER_STUDY.md) ·
[CLI.md](CLI.md) · [ARCHITECTURE.md](ARCHITECTURE.md).

## Current visual treatment

Defaults:

- bright yellow-green inner line, `#F6FF00`
- dark outer line, `#101010`
- 3 px inner / 7 px outer at 720p, scaled with frame height
- 8% interior tint
- solid stroke (`stroke_pattern=solid`)

A dark/bright dual boundary holds up better across mixed lighting than one fixed color.
Microsoft recommends outlining important gameplay objects, configurable colors, and
black/white boundaries where backgrounds vary:
[Xbox Accessibility Guideline 102: Contrast](https://learn.microsoft.com/en-us/xbox/accessibility/xbox-accessibility-guidelines/102).

W3C non-text contrast uses 3:1 against adjacent colors as an engineering target:
[Understanding Non-text Contrast](https://www.w3.org/WAI/WCAG22/understanding/non-text-contrast.html).
WCAG is a web standard; this does not make a CS2 overlay WCAG-conformant.

### Stroke patterns (second channel)

`OutlineStyle` supports a non-color pattern channel in addition to dual-contrast
colors:

| `stroke_pattern` | Meaning |
|---|---|
| `solid` | Continuous closed dual stroke (default; historical path). |
| `dashed` | Static dashes along the contour perimeter. |
| `dotted` | Static shorter on-runs (dots) along the perimeter. |

`dash_period_px` (default 12 at 720p) sets the arc-length period of one dash/dot cycle.
When `scale_with_frame` is enabled, the period scales with frame height the same way
line widths do. Patterns are geometric only: no temporal phase, animation, or
frame-to-frame dash state. Dual-contrast (≥3:1) and outer ≥ inner+2 geometry are
unchanged for all patterns.

### Optional render upgrades (static)

Optional treatments stay static (no pulse or flash):

| Field | Values | Role |
|---|---|---|
| `fill_mode` | `tint` (default) \| `halo` | Hard interior tint vs soft static glow (dilate + blur of the mask, outer color at `fill_opacity`). Dual-stroke still draws on top. |
| `halo_blur` | positive int (default 9) | Blur kernel size for halo fill. |
| `adapt_width_to_area` | bool (default false) | When true, scale stroke widths per instance from polygon area / frame area so small/distant players get thicker relative strokes (scale clamped ≈ [0.75, 2.5]; outer stays ≥ inner+2). |
| `outline_kernel` | `polyline` (default) \| `distance` \| `jfa` | Polyline dual-stroke (historical parity); `distance` uses OpenCV `distanceTransform` edge bands; `jfa` uses multi-step Jump Flood (seed filled/empty mask pixels, log₂ steps) for the same outer/inner band model. Both band kernels are static (same inputs → same pixels); JFA is pure-numpy field propagation rather than the OpenCV transform. |

CLI: `--fill-mode`, `--halo-blur`, `--adapt-width`, `--outline-kernel`. Defaults preserve the solid tint + polyline path.

## Configuration

List the complete machine-readable preset catalog:

```bash
uv run cs2-vision outline-presets
```

| Preset | Purpose |
|---|---|
| `high-visibility` | Bright yellow-green, dark boundary, restrained fill; solid dual-stroke. |
| `maximum-visibility` | Thick white-on-black boundary and stronger fill; solid dual-stroke. |
| `cyan-black` | Alternative that does not depend on red/green contrast; solid dual-stroke. |

All presets default to `stroke_pattern=solid`. Dashed/dotted patterns are available on
`OutlineStyle` and via CLI as a color-vision secondary signifier.

### Local preferences (`prefs show|set|reset`)

Outline preferences are a local JSON file (schema v1): named preset plus optional field
overrides. Manage them without re-typing every flag on each run:

```bash
# Motor-friendly: a named preset alone is enough
uv run cs2-vision prefs set --path artifacts/outline-prefs.json \
  --outline-preset maximum-visibility

# Optional overrides on top of the preset
uv run cs2-vision prefs set --path artifacts/outline-prefs.json --overwrite \
  --outline-preset cyan-black \
  --stroke-pattern dashed \
  --fill-opacity 0.12

uv run cs2-vision prefs show --path artifacts/outline-prefs.json
uv run cs2-vision prefs reset --path artifacts/outline-prefs.json --overwrite
```

`prefs set` / `prefs reset` require `--overwrite` when the destination already exists.
Invalid contrast or geometry fails closed before writing.

Apply a saved file on any outline run:

```bash
uv run cs2-vision outline \
  --input data/local-demo.mp4 \
  --model yolo26n-seg.onnx \
  --manifest artifacts/yolo26n-seg.model.json \
  --prefs artifacts/outline-prefs.json \
  --output artifacts/local-demo-outlined.mp4
```

### Style flags

The `outline` and `benchmark` commands expose:

- `--prefs PATH` local outline preferences JSON;
- `--outline-preset` (a preset alone is sufficient; no other style flags required);
- `--inner-color #RRGGBB`;
- `--outer-color #RRGGBB`;
- `--inner-width`;
- `--outer-width`;
- `--fill-opacity` from 0 to 0.35;
- `--fixed-widths` to disable automatic scaling above 720p;
- `--stroke-pattern solid|dashed|dotted`;
- `--dash-period PX` dash/dot period at 720p;
- `--fill-mode tint|halo` interior tint vs soft static halo;
- `--halo-blur PX` halo blur kernel (default 9);
- `--adapt-width` distance-adaptive per-instance stroke widths;
- `--outline-kernel polyline|distance|jfa` polyline dual-stroke vs distance-transform bands vs Jump Flood bands;
- `--role-config PATH` optional class-keyed multi-signifier treatment catalog JSON;
- `--cue-log PATH` optional offline instance enter/leave JSONL;
- `--preview-frame N` single-frame PNG / windowed A/B preview (see below);
- `--temporal-suppress` opt-in suppress-only anti-flash (see Stability);
- `--temporal-min-frames N` consecutive frames required before draw (default 2 when suppress is on).

Style field precedence is preset → prefs file → explicit CLI flags. Low-contrast
or invalid geometry fails closed before model load.

Every run summary records the actual colors, fill, source dimensions,
resolved pixel widths, configured stroke contrast, stroke pattern, and optional cue-log
path/event counts.

### Single-frame and short-segment preview

Compare outline treatments without encoding a full video:

```bash
# One frame as PNG (decode up to N; infer and render only that frame)
uv run cs2-vision outline \
  --input data/local-demo.mp4 \
  --model yolo26n-seg.onnx \
  --manifest artifacts/yolo26n-seg.model.json \
  --outline-preset maximum-visibility \
  --preview-frame 90 \
  --output artifacts/preview-maxvis.png

# Short MP4 segment without a full-file encode
uv run cs2-vision outline \
  --input data/local-demo.mp4 \
  --model yolo26n-seg.onnx \
  --manifest artifacts/yolo26n-seg.model.json \
  --outline-preset cyan-black \
  --max-seconds 3 \
  --output artifacts/preview-cyan.mp4
```

`--preview-frame` requires a `.png` `--output` and/or `--show`. Full encodes still use
`.mp4`. Short segments use `--max-frames` or `--max-seconds` with an MP4 output.

### Immediate off

Processing can be stopped without finishing the whole file:

| Mechanism | How |
|---|---|
| Window keys | With `--show`, press q or Esc (`termination_reason=user_stop`). |
| Shell interrupt | Ctrl+C aborts the process; partial outputs are discarded (not committed). |
| Duration bound | `--max-seconds S` ends after about S seconds of source time (`duration_limit`). |
| Frame bound | `--max-frames N` ends after N decoded frames (`frame_limit`; default 18000). |
| Single-frame preview | `--preview-frame N` stops after that frame (`preview_frame`). |

Partial MP4 / cue-log / PNG files are written next to the destination and only replaced
onto the final path after a successful run.

Live operators can use `cs2-vision gui` (or live hotkeys) to adjust style against real
scenes, load/save `cs2-vision-config.json`, and stop immediately. Offline prefs remain
available for batch outline runs.

## Motor access

The recorded-file workflow is bounded and unattended after launch. Named presets cut
down typing: `--outline-preset` alone, or `prefs set --outline-preset …`, is enough.
`outline-presets` and `prefs show|set|reset` give a deterministic config surface.
`--max-seconds` / `--max-frames` end a run without a keypress. `--preview-frame` supports
A/B comparison without a full encode. Live/GUI offer Start/Stop and preset buttons.

## Color vision

If ally/enemy (or other multi-class) distinction is used, relying only on color can
limit accessibility. Consider adding a second signifier such as line pattern, icon,
shape, audio, or haptic feedback. The renderer exposes a static `stroke_pattern`
channel (`solid` / `dashed` / `dotted`) and optional geometric markers for non-color
discrimination experiments.

Microsoft notes that red/green ally/enemy outlines can fail users with color-vision
deficiency and recommends multiple sensory channels for critical information:
[Xbox Accessibility Guideline 103](https://learn.microsoft.com/en-us/xbox/accessibility/xbox-accessibility-guidelines/103).

### Multi-signifier role treatments

Class-keyed role catalogs map each `InstanceMask.class_name` to a `RoleTreatment`:

| Channel | Options | Role |
|---|---|---|
| Dual-stroke colors | `#RRGGBB` inner/outer (≥3:1) | Primary visibility |
| `stroke_pattern` | `solid` \| `dashed` \| `dotted` | Non-color contour signifier |
| `marker` | `none` \| `triangle` \| `square` \| `chevron` | Shape at top of instance bbox |

When two or more non-default class entries are present, the catalog fails closed if
any pair is color-only (same pattern and both `marker=none`). Unknown class names fall
back to the catalog `default` treatment. With no catalog, the single-style path is
unchanged (historical pixel behavior).

```bash
uv run cs2-vision outline \
  --input data/local-demo.mp4 \
  --model yolo26n-seg.onnx \
  --manifest artifacts/yolo26n-seg.model.json \
  --role-config docs/examples/role-catalog.v1.json \
  --output artifacts/roles-demo.mp4
```

Example schema: [docs/examples/role-catalog.v1.json](examples/role-catalog.v1.json).
`RenderDiagnostics.roles_applied` reports per-key contour counts when a catalog is
active.

## Stability

Outlines should not animate or pulse. Detector jitter can flash, but holding a stale
mask can outline empty space or cross an occluder.

- each prediction carries its frame index
- the renderer discards mismatched frames
- inference and rendering are synchronous

### Opt-in suppress-only anti-flash (`--temporal-suppress`)

Default is frame-sync only (policy off). When a detector flickers a player for one
frame, the outline can flash. An optional suppress-only filter reduces that without
holding geometry:

| Behaviour | Default (off) | `--temporal-suppress` |
|---|---|---|
| Unstable one-frame blip | drawn | suppressed until seen on N consecutive frames |
| Stable multi-frame detection | drawn | drawn after N consecutive matches (default N=2) |
| Detector dropout / miss | no outline | no outline (no invent or hold-last-mask) |

Association is a consecutive-frame heuristic (axis-aligned box IoU and/or centroid
proximity within class). There are no track IDs and no hold-last-mask: holding a
stale contour is an occlusion risk and is intentionally not the default (and not
implemented in v1).

```bash
# Suppress single-frame flashes; require 2 consecutive matched frames to draw
uv run cs2-vision outline \
  --input data/local-demo.mp4 \
  --model yolo26n-seg.onnx \
  --manifest artifacts/yolo26n-seg.model.json \
  --temporal-suppress \
  --output artifacts/local-demo-stable.mp4

# Stricter gate (research): require 3 consecutive frames
uv run cs2-vision outline \
  --input data/local-demo.mp4 \
  --model yolo26n-seg.onnx \
  --manifest artifacts/yolo26n-seg.model.json \
  --temporal-suppress --temporal-min-frames 3 \
  --output artifacts/local-demo-stable3.mp4
```

CLI:

- `--temporal-suppress` - enable the filter (opt-in);
- `--temporal-min-frames N` - consecutive matched frames required before draw
 (default 2 when the feature is enabled).

The run summary field `temporal_suppressed` counts how many predicted instances were
dropped by the filter (0 when the policy is off).

## User evaluation

Proxies help early, but testing with gamers who have the target disabilities is what
counts. A pilot study should cover:

- detection usefulness versus visual clutter;
- preferred line width, color, opacity, and fill;
- small/distant player visibility;
- comfort during fast camera motion, smoke, flash, and high-detail scenes;
- ability to disable or adjust the effect quickly;
- whether outline errors are confusing or unsafe;
- whether a non-visual cue is also needed.

Operational protocol, `study.json` schema, rating JSONL, and
`cs2-vision study-render` / `study-aggregate` tooling are documented in
[USER_STUDY.md](USER_STUDY.md).

The SeeingVR study evaluated edge enhancement and semantic highlighting with eleven
participants with low vision and found that preferences varied, supporting
configurability:
[SeeingVR paper](https://www.microsoft.com/en-us/research/wp-content/uploads/2019/01/SeeingVRchi2019.pdf).
