# Architecture

Periscope is an umbrella repository, not a single application. It contains two
independently licensed and independently built research products that share a
subject area but no runtime code.

## System responsibilities

### Radar

Radar is a C++20 red/blue teaching lab. Its deterministic simulation models
observable attack and detection traces. Scenario pairs apply red behavior and
evaluate blue responses. Executables compose those scenarios with either the
simulation adapter or explicitly enabled real-platform research adapters.

Dependency direction:

```text
apps -> scenarios -> application -> simulation/domain
apps ---------------------------> selected adapters
real adapters ------------------> application ports/domain contracts
domain and simulation -X-------> real adapters
```

The real Windows, Linux, kernel, DMA, VMX, SMM, UEFI, GPU, network, driver, and
firmware lanes are intentional teaching material. They are never part of the
default simulation dependency graph and remain explicit build/runtime opt-ins.

### Vision

Vision is a Python package for visible-pixel accessibility research. Frames
enter from local video, capture devices, or screen capture. A selected model
produces masks, optional temporal policy suppresses unstable detections, and a
renderer sends the result to a file, display, separate overlay window, or a
shared-memory ring. In the primary deployment (`vision/docs/CONTOUR_PIPELINE.md`)
the whole pipeline runs on a second Windows machine against HDMI capture and
presents 1:1 fullscreen, so no overlay window exists on the gaming machine.

Dependency direction:

```text
interfaces -> application/workflows -> domain
interfaces/workflows -------------> reusable adapters
adapters --------------------------> domain and application ports
domain/application -X-------------> workflows, interfaces, or concrete adapters
```

The CLI and GUI are composition edges. Shared behavior belongs in application
services, not in command handlers or widgets. Deterministic schemas and value
objects belong in the domain. Reusable model-runtime, capture, rendering, and
platform integrations are adapters. A vertical batch workflow may localize a
one-off filesystem or OpenCV operation when introducing a port would add no
useful substitution or test seam; those effects must not leak into the domain
or application core.

Capture and presentation split the same way: the 1:1 present geometry is a pure
domain-adjacent contract, the Win32 window and the shared-memory ring are
adapters. Capture reconnect/backoff and the stale-frame bound live in the capture
adapter, never in the live pipeline loop.

## External contracts

- Vision preserves the `cs2-vision-access` distribution, `cs2-vision` command,
  `python -m cs2_vision_access`, documented public imports, backend names,
  configuration and artifact schemas, model checksum binding, and pixel-only
  safety boundary.
- Radar preserves documented executable and CMake option names, simulation as
  the default, strategy-lab commands, offset snapshot formats, and versioned
  driver, firmware, IPC, and network layouts.
- Source-tree module names, private helpers, CMake library decomposition, and
  directory locations are internal unless active documentation says otherwise.

## Placement guide

- Put deterministic concepts and policies in `domain` or `simulation`.
- Put use-case sequencing in `application` or `workflows`.
- Put OS, hardware, model-runtime, network, UI-toolkit, and filesystem effects
  in adapters or interfaces.
- Put executable and command composition at the outer edge.
- Add a shared abstraction only for a real boundary with more than one useful
  implementation or a test substitute.

`python3 scripts/check_architecture.py` enforces the dependency rules that are
cheap and valuable to check mechanically.
