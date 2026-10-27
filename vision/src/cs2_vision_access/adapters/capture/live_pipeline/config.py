"""Pipeline data models and configuration."""

from __future__ import annotations

import threading
from dataclasses import dataclass, field

from cs2_vision_access.adapters.capture.base import CaptureConfig
from cs2_vision_access.adapters.capture.hotkeys import LiveControlState
from cs2_vision_access.adapters.capture.outputs import OutputSink
from cs2_vision_access.adapters.capture.overlay import OverlayWindow
from cs2_vision_access.adapters.capture.screen import ScreenCapturer
from cs2_vision_access.application.live.inference.temporal import TemporalStabilityConfig
from cs2_vision_access.domain.safety import MAX_FRAME_PIXELS


class LivePipelineError(RuntimeError):
    """Live pipeline encountered a non-recoverable error."""


@dataclass(frozen=True)
class LivePipelineConfig:
    capture: CaptureConfig | ScreenCapturer = field(default_factory=CaptureConfig)
    max_frames: int = 0
    display_scale: float = 0.5
    frame_skip_threshold: int = 2
    diagnostic_interval: float = 2.0
    headless: bool = False
    alpha_output_mode: str = "overlay"
    enable_alpha_fill: bool = False
    temporal_config: TemporalStabilityConfig | None = None
    enable_hotkeys: bool = True
    output_sink: OutputSink | None = None
    max_consecutive_read_failures: int = 3
    max_frame_pixels: int = MAX_FRAME_PIXELS
    overlay_window: OverlayWindow | None = None
    control_state: LiveControlState | None = None
    terminate_event: threading.Event | None = None
    # Bound the pipeline's own queue: frames older than this (ms of capture
    # time) are dropped before inference rather than being presented late.
    # 0 disables the age check (the frame_skip_threshold logic still applies).
    max_capture_age_ms: float = 0.0


@dataclass(frozen=True)
class LiveRunSummary:
    frames_processed: int
    frames_inferred: int
    frames_dropped: int
    instances_predicted: int
    instances_outlined: int
    temporal_suppressed: int
    inference_ms_p50: float
    inference_ms_p95: float
    pipeline_ms_p50: float
    pipeline_ms_p95: float
    elapsed_seconds: float
    capture_fps: float
    throughput_fps: float
    termination_reason: str
    capture_age_dropped: int = 0
