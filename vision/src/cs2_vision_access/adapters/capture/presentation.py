"""Fullscreen contour presenter for the second-device pipeline.

The primary vision topology is:

    gaming PC --HDMI--> splitter --> capture card --> device 2 (Windows)
                                                        |
                                              capture -> segment -> outline
                                                        |
                                              composite 1:1 over the frame
                                                        |
                                              fullscreen present

Device 2 opens a borderless, cursor-less, topmost window sized exactly to the
monitor the player looks at, and blits the composited frame at native
resolution. Because the pixels shown are the pixels captured, alignment is a
solved problem: nothing is scaled, nothing is moved.

Only the composited output ever reaches this window — inference diagnostics,
pause badges and hotkey hints stay on the preview path. On Windows the window
can additionally exclude itself from screen capture (streamproof), so a
recording or stream of the gaming PC never shows the outline even when the
overlay runs on the same machine.

The pure geometry lives here so tests can drive it without a display; the
ctypes/Win32 specifics live in the win32 overlay backend.
"""

from __future__ import annotations

from dataclasses import dataclass

from cs2_vision_access.domain.outline import OutlineStyle

# Alpha/green compositing backgrounds; green supports hardware chroma keying.
PRESENTATION_MODES = frozenset({"overlay", "alpha", "green"})

# Local idleness above which the display must not be allowed to sleep.
_DISPLAY_IDLE_SECONDS = 60


@dataclass(frozen=True)
class PresentGeometry:
    """Resolved presenter geometry for one monitor.

    Attributes:
        width: Native pixels across the target monitor.
        height: Native pixels down the target monitor.
        offset_x: Horizontal card-to-display alignment correction.
        offset_y: Vertical card-to-display alignment correction.
        monitor_index: 1-based monitor index, 0 when unresolved.
    """

    width: int
    height: int
    offset_x: int = 0
    offset_y: int = 0
    monitor_index: int = 0

    def __post_init__(self) -> None:
        if self.width <= 0 or self.height <= 0:
            raise ValueError("present geometry must be positive")
        if not isinstance(self.offset_x, int) or not isinstance(self.offset_y, int):
            raise ValueError("present offsets must be integers")


def resolve_present_size(
    capture_width: int,
    capture_height: int,
    present_width: int,
    present_height: int,
) -> PresentGeometry:
    """Resolve the present geometry for a capture of the given size.

    The presenter never scales: scaling would blur the game image and smear the
    contours. When the capture does not match the monitor the caller must fix
    the capture side; this function only reports the mismatch.
    """
    if capture_width <= 0 or capture_height <= 0:
        raise ValueError("capture size must be positive")
    if present_width <= 0 or present_height <= 0:
        raise ValueError("present size must be positive")
    if (capture_width, capture_height) != (present_width, present_height):
        raise ValueError(
            f"capture {capture_width}x{capture_height} does not match the "
            f"present surface {present_width}x{present_height}; fix the "
            "capture resolution or the monitor selection instead of scaling"
        )
    return PresentGeometry(width=present_width, height=present_height)


def validate_present_mode(mode: str) -> str:
    """Reject unsupported presentation modes early, with the valid set."""
    if mode not in PRESENTATION_MODES:
        valid = ", ".join(sorted(PRESENTATION_MODES))
        raise ValueError(f"unsupported presentation mode {mode!r}; expected one of: {valid}")
    return mode


@dataclass(frozen=True)
class CompositeOptions:
    """Options applied while compositing contours over captured pixels.

    ``outline`` carries the dual-stroke style; ``contour_only`` renders onto a
    transparent or chroma-key background instead of the captured pixels.
    """

    outline: OutlineStyle
    contour_only: bool = False
    show_fill: bool = False
