"""HDMI/SDI capture device discovery and OpenCV VideoCapture wrapper.

Supports:
  - Enumerating connected capture devices via DirectShow (Windows) / AVFoundation (macOS)
  - Opening a device by index, partial name match, or DirectShow device path
  - Configuring resolution, FPS, and codec fourcc
  - Frame timing diagnostics (actual vs requested FPS)
"""

from __future__ import annotations

import contextlib
import re
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any

# OpenCV is a hard dependency; keep the name on the module so tests can patch it.
try:
    import cv2
except ImportError:  # pragma: no cover - install path surfaces a clearer error below
    cv2 = None  # type: ignore[assignment]


class LiveCaptureError(RuntimeError):
    """Could not open or read from the capture device."""


@dataclass(frozen=True)
class CaptureDeviceInfo:
    """Metadata about an available capture device."""

    index: int
    name: str
    backend: str = "auto"


@dataclass(frozen=True)
class CaptureConfig:
    """Configuration for opening a live capture device.

    ``source`` can be:
      - An integer device index (0, 1, 2, …)
      - A string matching a device name substring (case-insensitive)
      - A ``directshow:...`` path on Windows
      - A file path to a local video file (for testing the pipeline without a device)

    ``preferred_width`` / ``preferred_height``: target capture resolution (best-effort).
    ``preferred_fps``: target frame rate (best-effort).
    ``backend``: OpenCV ``cv2.CAP_*`` hint (``auto``, ``dshow``, ``avfoundation``).
    ``fourcc``: optional codec negotiation (e.g. ``"MJPG"``, ``"YUY2"``) applied
      best-effort after opening. Capture cards advertise MJPG at high frame
      rates over USB3; uncompressed YUY2 avoids a compression stage at the cost
      of cable bandwidth.
    """

    source: int | str = 0
    preferred_width: int = 1920
    preferred_height: int = 1080
    preferred_fps: float = 60.0
    backend: str = "auto"
    buffer_size: int = 1  # cv2.CAP_PROP_BUFFERSIZE hint
    fourcc: str | None = None


def _require_cv2() -> Any:
    """Return the module-level cv2 binding or raise a clear install error."""
    if cv2 is None:
        raise LiveCaptureError("OpenCV is required for live capture; install project dependencies")
    return cv2


def _default_backend() -> int | None:
    """Return the best OpenCV backend hint for the current platform."""
    if cv2 is None:
        return None
    if sys.platform == "win32":
        return getattr(cv2, "CAP_DSHOW", None)
    if sys.platform == "darwin":
        return getattr(cv2, "CAP_AVFOUNDATION", None)
    return None


def list_capture_devices(max_devices: int = 10) -> list[CaptureDeviceInfo]:
    """Probe connected capture devices and return metadata for each.

    Uses OpenCV's ``CAP_DSHOW`` on Windows, ``CAP_AVFOUNDATION`` on macOS,
    and ``CAP_ANY`` on Linux. Each device is tested by attempting to open it
    and reading its name property.
    """
    cv2 = _require_cv2()

    backend = _default_backend()
    devices: list[CaptureDeviceInfo] = []
    for index in range(max_devices):
        cap = cv2.VideoCapture(index, backend) if backend is not None else cv2.VideoCapture(index)
        if cap.isOpened():
            cap.get(cv2.CAP_PROP_FRAME_WIDTH)
            name_str = _resolve_device_name(cap, index)
            cap.release()
            devices.append(CaptureDeviceInfo(index=index, name=name_str, backend="auto"))
        else:
            cap.release()
            # Stop probing at first gap on most backends
            if index > 0 and not devices:
                break
    return devices


def _resolve_device_name(cap: Any, index: int) -> str:
    """Try to get a human-readable device name from the capture object."""
    if cv2 is None:
        return str(index)

    # OpenCV doesn't provide a universal name property, so we fall back to
    # the best available indicator.
    width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    fps = cap.get(cv2.CAP_PROP_FPS)
    backend = cap.get(cv2.CAP_PROP_BACKEND)
    if width > 0 and height > 0:
        return f"Device {index} ({width}x{height} @ {fps:.0f} fps, backend={int(backend)})"
    return f"Device {index}"


def open_capture(
    config: CaptureConfig,
    *,
    prefer_msmf: bool = False,
) -> Any:
    """Open a capture device or local video file.

    Returns an ``cv2.VideoCapture`` instance (ready for ``read()``).

    Raises:
        LiveCaptureError: if the device cannot be opened at the specified source.
    """
    cv2 = _require_cv2()

    # Determine the OpenCV backend API preference.
    # On Windows, default to DirectShow (CAP_DSHOW) — MSMF is flaky with many
    # capture cards and webcams. On macOS, prefer AVFoundation.
    backend_map: dict[str, int | None] = {
        "auto": _default_backend(),
        "dshow": cv2.CAP_DSHOW if hasattr(cv2, "CAP_DSHOW") else None,
        "avfoundation": (cv2.CAP_AVFOUNDATION if hasattr(cv2, "CAP_AVFOUNDATION") else None),
        "msmf": cv2.CAP_MSMF if hasattr(cv2, "CAP_MSMF") else None,
        "any": cv2.CAP_ANY,
        "v4l2": cv2.CAP_V4L2 if hasattr(cv2, "CAP_V4L2") else None,
    }
    if config.backend not in backend_map:
        raise LiveCaptureError(
            f"Unsupported backend {config.backend!r}; supported: {', '.join(sorted(backend_map))}"
        )
    api_preference = backend_map[config.backend]

    source = config.source

    # Case 1: integer device index
    if isinstance(source, int):
        cap = (
            cv2.VideoCapture(source, api_preference)
            if api_preference is not None
            else cv2.VideoCapture(source)
        )
        if not cap.isOpened():
            raise LiveCaptureError(f"Could not open capture device {source}")
        _configure_capture(cap, config)
        return cap

    # Case 2: local file path
    source_str = str(source)
    if isinstance(source, (str, Path)):
        path = Path(source_str)
        if path.is_file():
            cap = cv2.VideoCapture(str(path.resolve()))
            if not cap.isOpened():
                raise LiveCaptureError(f"Could not open video file: {source}")
            return cap

    # Case 3: try as a name match against enumerated devices
    devices = list_capture_devices()
    matched = [d for d in devices if re.search(re.escape(source_str), d.name, re.IGNORECASE)]
    if matched:
        cap = (
            cv2.VideoCapture(matched[0].index, api_preference)
            if api_preference is not None
            else cv2.VideoCapture(matched[0].index)
        )
        if not cap.isOpened():
            raise LiveCaptureError(
                f"Could not open capture device '{source_str}' "
                f"(resolved to index {matched[0].index})"
            )
        _configure_capture(cap, config)
        return cap

    # Case 4: try DirectShow path on Windows (format: "directshow:device name")
    if source_str.lower().startswith("directshow:"):
        dshow_name = source_str[11:].strip()
        cap = cv2.VideoCapture(dshow_name, cv2.CAP_DSHOW)
        if cap.isOpened():
            _configure_capture(cap, config)
            return cap
        raise LiveCaptureError(f"Could not open DirectShow device: {dshow_name}")

    raise LiveCaptureError(
        f"Could not resolve capture source {source!r}. "
        "Try an integer device index, a device name substring, or a local file path."
    )


def _configure_capture(cap: Any, config: CaptureConfig) -> None:
    """Apply resolution, FPS, codec, and buffer size hints to a VideoCapture."""
    if cv2 is None:
        return

    if config.buffer_size >= 1:
        with contextlib.suppress(Exception):
            cap.set(cv2.CAP_PROP_BUFFERSIZE, config.buffer_size)

    # Set resolution (best-effort; hardware may override)
    if config.preferred_width > 0 and config.preferred_height > 0:
        cap.set(cv2.CAP_PROP_FRAME_WIDTH, config.preferred_width)
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, config.preferred_height)

    # Set FPS (best-effort)
    if config.preferred_fps > 0:
        cap.set(cv2.CAP_PROP_FPS, config.preferred_fps)

    # Codec negotiation (best-effort). MJPG matters on USB3 capture sticks:
    # uncompressed 1080p60 often will not enumerate at all without it.
    if config.fourcc:
        with contextlib.suppress(Exception):
            fourcc = cv2.VideoWriter_fourcc(*config.fourcc.upper())
            cap.set(cv2.CAP_PROP_FOURCC, fourcc)


@dataclass
class FrameTimingDiagnostics:
    """Running frame timing diagnostics for live capture."""

    frames_received: int = 0
    frames_dropped: int = 0
    last_frame_time: float = 0.0
    frame_interval_target_ms: float = 16.667  # 60 fps default
    actual_fps: float = 0.0

    def record_received(self) -> None:
        """Call after successfully reading a frame."""
        now = time.perf_counter()
        if self.last_frame_time > 0:
            elapsed_since_last = now - self.last_frame_time
            actual_interval = elapsed_since_last * 1000.0
            if actual_interval > self.frame_interval_target_ms * 1.5:
                self.frames_dropped += int(actual_interval / self.frame_interval_target_ms)
            # Rolling FPS from the last-frame interval
            if elapsed_since_last > 0:
                self.actual_fps = 1.0 / elapsed_since_last
        self.last_frame_time = now
        self.frames_received += 1
