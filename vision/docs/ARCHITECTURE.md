# Architecture

Vision is the Python track of Periscope. It outlines visible players using only
local media, capture-device frames, or screen pixels. It does not open the game
process, read memory, synthesize input, or provide aim automation.

The installed package is `cs2_vision_access`. User entry points remain the
`cs2-vision` command and `python -m cs2_vision_access`.

## Runtime flow

```text
CLI or GUI
  -> live application service or batch workflow
  -> capture port -> capture adapter
  -> model-session port -> selected model adapter
  -> temporal and outline policy
  -> rendering/output ports -> display, overlay, or file adapters
```

Workflows for datasets, labeling, training, evaluation, studies, and bakeoffs
reuse the same domain contracts without depending on the CLI or GUI.

## Package map

| Path | Ownership |
| --- | --- |
| `domain/` | Predictions, styles, manifest metadata, dataset schemas, and pure policies |
| `application/` | Live/offline use cases, configuration, model-asset services, and ports |
| `workflows/` | Dataset, labeling, training, evaluation, study, and bakeoff orchestration |
| `adapters/` | Capture (including fourcc negotiation, reconnect, shared-memory sink, fullscreen presenter), model runtime, OpenCV rendering, OS overlays |
| `interfaces/cli/` | Command parsing and presentation |
| `interfaces/gui/` | Tk dashboard composition and presentation |

Documented former package paths may remain as small compatibility facades. They
must not own implementation or become dependencies of the new inner layers.

## Dependency direction

```text
interfaces -> application/workflows -> domain
interfaces/workflows -------------> reusable adapters
adapters --------------------------> domain and application ports
domain/application -X-------------> workflows, interfaces, or concrete adapters
```

Application ports are limited to volatile boundaries such as capture, model
sessions, artifact storage, downloads, display/overlay/present, clocks, and
thread control. Pure functions do not need interfaces merely to satisfy a
pattern. A vertical batch workflow may keep a one-off filesystem or OpenCV
operation local when a port would add no useful substitute or test seam.
Reusable live, model, rendering, and platform integrations belong in adapters;
concrete effects never belong in domain or application code.

The second-device composite splits that way deliberately: `presentation.py`
holds the pure 1:1 geometry contract and `presenter.py` holds the Win32 window,
so geometry stays testable without a display. The shared-memory ring in
`adapters/capture/sinks/shared.py` is a sink like any other; cross-process
consumers read it with `read_shared_frame`.

## Configuration and artifacts

Live CLI and GUI resolve the same immutable runtime configuration and continue
to read schema version 1. Training, study, and dataset configurations remain
separate bounded schemas. Explicit CLI values take precedence over persisted
or discovered values.

ONNX models that require manifests remain bound to their filename, task, class
map, and SHA-256. Dataset and evaluation formats retain their documented schema
versions and class meanings.

## Verification

From the repository root:

```bash
python3 scripts/check_architecture.py
python3 scripts/verify.py vision-cpu
```

The CPU lane covers dependency rules, lint, formatting, strict typing, tests,
and package construction. CUDA, live capture, GUI display, overlays, capture
cards, the fullscreen presenter, remote services, and multi-epoch training
require separate provisioned environments.

`vision/tests/` is gitignored at the root today, so a fresh clone has no test
directory. Restore the tracked-suite intent or drop the ignore before relying on
`pytest` in a clean checkout.
