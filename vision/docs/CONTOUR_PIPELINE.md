# Contour composite pipeline — second-device setup

The vision track never opens the game process, reads its memory, or injects
input — so there is nothing for an anti-cheat to observe on the gaming machine.
This document describes the primary deployment: an HDMI-only topology with the
inference and compositing on a second Windows machine.

```
gaming PC ──HDMI──▶ splitter ──▶ capture card ─▶ device 2 (Windows)
    │                                (USB3)        capture → segment → outline
    └──▶ local monitor (optional, direct)                          │
                                                         composite 1:1
                                                                   │
                                                     fullscreen present
                                                                   │
                                                        player monitor
```

## Why signal-only is undetectable

An anti-cheat sees what runs on the gaming machine: drivers, handles to the
game, injected modules, simulated input. This pipeline has none of those:

| Concern | Radar track (`radar/`) | Vision track |
|---------|------------------------|--------------|
| Game process opened | Yes (RPM / donor) | Never |
| Handles to cs2.exe | Yes — attach ladder manages them | None |
| Overlay on game machine | D3D11 layered window | None required |
| Input simulation | Never | Never |
| Anti-cheat-visible surface | Reduced by the stealth posture | Nothing runs there |

The HDMI path is a receive-only consumer of the video signal. The gaming PC
cannot tell a capture card from a monitor, and the split happens before
anything that runs code.

## Hardware

| Item | Notes |
|------|-------|
| HDMI splitter | 1-in 2-out, powered, **EDID clone** mode. The splitter must present the original monitor's EDID to the gaming PC, or Windows may renegotiate a different resolution or drop to 30 Hz. |
| Capture card | USB3 UVC. 1080p60 requires MJPG (``--fourcc MJPG``); YUY2 at 1080p60 needs USB3 + low latency. Passthrough models (game on one HDMI out, USB to device 2) avoid the splitter entirely. |
| Device 2 | Windows 10 2004+ for ``WDA_EXCLUDEFROMCAPTURE``; any box that drives 1080p+ can run inference. |

### Latency budget (typical, 1080p60)

| Stage | Latency |
|-------|---------|
| Splitter / passthrough | 1–2 frames (16–33 ms) |
| Capture (MJPG) | 1–2 frames |
| Segment + composite (GPU) | 5–15 ms |
| Present (fullscreen flip) | < 1 frame |

Target end-to-end ≤ 60 ms. Use ``--max-capture-age-ms 40`` so the pipeline
drops frames that are already late instead of stacking them: a queue that
grows without bound turns "a little behind" into "unwatchably behind".

## Capture configuration

```bash
cs2-vision live \
  --source-type capture-device \
  --input-device 0 \
  --fourcc MJPG \
  --width 1920 --height 1080 --fps 60 \
  --class-name player \
  --output-mode overlay \
  --output-stream shm:contours \
  --max-capture-age-ms 40
```

- ``--fourcc MJPG`` negotiates the compressed mode the card supports at high
  frame rates. Use ``YUY2`` if the card misbehaves (some re-encode MJPG
  internally anyway).
- ``--output-mode overlay`` composites outlines onto the captured frame. This
  is what the second device shows. (`alpha` / `green` remain available for
  OBS/chroma-key workflows — see [LIVE_MODE.md](LIVE_MODE.md).)
- ``--output-stream shm:contours`` additionally publishes the composite into a
  shared-memory ring for a second process on device 2.

## The compositor / presenter

``Win32FullscreenPresenter`` (adapters/capture/presenter.py) opens a borderless
topmost window sized to the target monitor and blits the composited frames at
native resolution. Rules the geometry contract enforces:

1. **1:1 only.** The captured size must equal the presented size. A mismatch
   raises rather than scales — scaling blurs the game and smears the outlines.
2. **Offsets, not scaling.** If the card's active area does not start at the
   display's top-left, apply ``--present-offset-x/y``.
3. **Cursor hidden.** The presenter hides the cursor over the window; there is
   no visible chrome, no title bar, no diagnostics.
4. **Streamproof.** ``streamproof=True`` applies ``WDA_EXCLUDEFROMCAPTURE`` so
   any capture of device 2's output (game recording, screenshots) does not
   include the composite.

Launch order: presenter first, then the pipeline. The presenter window comes up
topmost and stays there; the compositor then blits into it.

## Single-machine alternative

The same pipeline works on the gaming machine with ``--overlay`` (Win32 layered
window above the game). In that mode the composite is alpha-only over the game,
and the overlay can exclude itself from capture. This is simpler but puts a
window on the gaming machine — the radar track's stealth posture and the vision
track's process isolation are two different ways to manage that surface.

## Verification

```bash
python3 scripts/verify.py all     # ruff, mypy, pytest, build
cs2-vision live --list-devices   # card enumeration
```

Camera checks:

1. ``--source-type capture-device --input-device 0 --output-mode overlay`` with
   no model: confirm the card's raw feed is visible end-to-end.
2. Add the segmenter: outlines on top of the composite.
3. ``--max-capture-age-ms 40``: confirm the composite stays real-time.

## Acknowledgements and compliance

This pipeline is a video consumer. Whether using it complies with the rules of
your lobby, tournament, or platform is between you and that platform — the
repository does not police operators (see [SECURITY.md](SECURITY.md)).
