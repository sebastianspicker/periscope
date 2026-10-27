"""Capture management — device opening, frame reading, validation, reconnection.

Encapsulates all capture-device and screen-capture concerns so the main
pipeline loop focuses on inference and rendering.
"""

from __future__ import annotations

import time
from dataclasses import dataclass
from typing import Any

import numpy as np

from cs2_vision_access.adapters.capture.base import (
    CaptureConfig,
    FrameTimingDiagnostics,
    open_capture,
)
from cs2_vision_access.adapters.capture.screen import ScreenCapturer
from cs2_vision_access.domain.safety import MAX_FRAME_PIXELS, validate_decoded_frame


@dataclass(frozen=True)
class CaptureInfo:
    """Resolved capture properties determined at open time."""

    width: int
    height: int
    fps: float
    source_type: str  # "capture-device" | "screen" | "file"


class CaptureError(RuntimeError):
    """Non-recoverable capture failure."""


class CaptureManager:
    """Manages a capture source (device, screen, or file) with validation and recovery.

    Args:
        capture_config: ``CaptureConfig`` or ``ScreenCapturer`` instance.
        cv2_module: The ``cv2`` module (injected to keep lazy import).
        max_frame_pixels: Maximum acceptable frame resolution in pixels.
        max_consecutive_read_failures: Failures before reconnection is attempted.
    """

    def __init__(
        self,
        capture_config: CaptureConfig | ScreenCapturer,
        cv2_module: Any,
        *,
        max_frame_pixels: int = MAX_FRAME_PIXELS,
        max_consecutive_read_failures: int = 3,
    ) -> None:
        self._config = capture_config
        self._cv2 = cv2_module
        self._max_frame_pixels = max_frame_pixels
        self._max_failures = max_consecutive_read_failures
        self._cap: Any = None
        self._info: CaptureInfo | None = None
        self._initial_dimensions: tuple[int, int] | None = None
        self._timing = FrameTimingDiagnostics()

    @property
    def info(self) -> CaptureInfo:
        info = self._info
        if info is None:
            raise CaptureError("capture not opened yet")
        return info

    @property
    def timing(self) -> FrameTimingDiagnostics:
        return self._timing

    # ------------------------------------------------------------------
    # Lifecycle
    # ------------------------------------------------------------------

    def open(self) -> None:
        """Open the capture source and determine its properties."""
        cv2 = self._cv2
        config = self._config

        self._cap = config if isinstance(config, ScreenCapturer) else open_capture(config)
        if not self._cap.isOpened():
            raise CaptureError("Capture device did not open.")

        width = int(self._cap.get(cv2.CAP_PROP_FRAME_WIDTH))
        height = int(self._cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
        fps = float(self._cap.get(cv2.CAP_PROP_FPS))

        if self._max_frame_pixels <= 0:
            self._cap.release()
            raise CaptureError("max_frame_pixels must be positive")
        if width <= 0 or height <= 0:
            self._cap.release()
            raise CaptureError(f"Capture device reported invalid resolution {width}x{height}.")
        if width * height > self._max_frame_pixels:
            self._cap.release()
            raise CaptureError(
                f"Capture resolution {width}x{height} exceeds the "
                f"{self._max_frame_pixels}-pixel safety limit."
            )
        if not (0.1 <= fps <= 1000):
            fps = 60.0

        source_type = "screen" if isinstance(config, ScreenCapturer) else "capture-device"
        if isinstance(config, CaptureConfig):
            candidate = config.source
            if isinstance(candidate, (str,)) and not candidate.lstrip("-").isdigit():
                source_type = "file"

        self._info = CaptureInfo(width=width, height=height, fps=fps, source_type=source_type)
        self._initial_dimensions = (width, height)
        self._timing.frame_interval_target_ms = 1000.0 / fps

    def close(self) -> None:
        """Release the capture source."""
        if self._cap is not None:
            self._cap.release()

    # ------------------------------------------------------------------
    # Frame reading
    # ------------------------------------------------------------------

    def read(self, frame_index: int) -> tuple[bool, np.ndarray | None, str]:
        """Read and validate the next frame.

        Args:
            frame_index: Current frame index (for diagnostics).

        Returns:
            ``(ok, frame, reason)`` where ``ok`` is False when the capture
            has ended or failed, and ``reason`` describes the termination.
        """
        ok, frame = self._cap.read()
        if not ok:
            return self._handle_read_failure(frame_index)

        self._timing.record_received()
        error = self._validate_frame(frame)
        if error:
            return False, None, error

        return True, frame, ""

    def _handle_read_failure(self, frame_index: int) -> tuple[bool, None, str]:
        """Handle a failed frame read with reconnection logic.

        Reconnect attempts back off (1, 2, 4 s) so a card renegotiating its
        signal — or a transient USB fault — cannot turn into a hot loop.
        """
        if not hasattr(self, "_failures"):
            self._failures = 0
        self._failures += 1
        attempt = getattr(self, "_reconnect_attempted", 0)

        if self._failures < self._max_failures:
            return False, None, ""

        if not isinstance(self._config, ScreenCapturer) and attempt < 3:
            backoff = 2.0**attempt  # 1, 2, 4 seconds
            print(
                f"[live] capture read failed "
                f"({self._failures} consecutive); reconnecting in {backoff:.0f}s"
            )
            time.sleep(backoff)
            try:
                if self._cap is not None:
                    self._cap.release()
                self._cap = open_capture(self._config)
                if not self._cap.isOpened():
                    raise CaptureError("Capture device did not open after reconnect.")
            except Exception as error:
                print(f"[live] capture reconnect failed: {error}")
                self._reconnect_attempted = attempt + 1
                self._failures = 0
                return False, None, ""
            self._reconnect_attempted = 0
            self._failures = 0
            self._drain_stale_frames()
            print("[live] capture reconnect succeeded")
            return False, None, ""

        return False, None, "capture_failure"

    def _drain_stale_frames(self, max_drain: int = 10) -> None:
        """Read past stale buffers queued while the device was away.

        Capture drivers buffer internally; reconnecting without draining would
        present a frame from before the outage. Best-effort, bounded.
        """
        if self._cap is None:
            return
        drained = 0
        while drained < max_drain:
            try:
                ok, _frame = self._cap.read()
            except Exception:
                break
            if not ok:
                break
            drained += 1
        if drained:
            print(f"[live] drained {drained} buffered frame(s) after reconnect")

    def _validate_frame(self, frame: np.ndarray | None) -> str:
        """Validate a decoded frame. Returns empty string on success."""
        if frame is None:
            return "received an empty frame"

        if not isinstance(frame, np.ndarray) or frame.dtype != np.uint8:
            return "received frame with unsupported dtype"

        try:
            dimensions = validate_decoded_frame(frame, max_frame_pixels=self._max_frame_pixels)
        except ValueError as error:
            return f"invalid decoded frame: {error}"

        if self._initial_dimensions is not None and dimensions != self._initial_dimensions:
            print(
                f"[live] capture resolution changed from "
                f"{self._initial_dimensions[0]}x{self._initial_dimensions[1]} to "
                f"{dimensions[0]}x{dimensions[1]}"
            )

        # ---- Black-frame guard ----
        # Detect self-capture feedback: when the overlay window covers the
        # capture region, every frame comes back black (or near-black).
        # Check the first few frames; if all are black, emit a clear diagnostic.
        if not hasattr(self, "_black_frame_count"):
            self._black_frame_count = 0
        if self._black_frame_count < 10:
            mean_luma = float(np.mean(frame))
            if mean_luma < 1.0:  # essentially black
                self._black_frame_count += 1
                if self._black_frame_count >= 5:
                    print(
                        f"[live] WARNING: {self._black_frame_count} consecutive near-black frames. "
                        "This may indicate the overlay window is covering the capture source. "
                        "Ensure CS2 is in borderless windowed mode and the overlay is not "
                        "positioned over the captured region."
                    )
            else:
                self._black_frame_count = 0  # reset on any non-black frame

        return ""

    def __enter__(self) -> CaptureManager:
        self.open()
        return self

    def __exit__(self, *args: object) -> None:
        self.close()
