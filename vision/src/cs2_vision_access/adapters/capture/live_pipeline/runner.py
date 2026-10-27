"""Orchestrator for the real-time live capture and outline pipeline."""

from __future__ import annotations

import time
from collections.abc import Callable
from typing import Any

import numpy as np

from cs2_vision_access.adapters.capture.display import DisplayManager
from cs2_vision_access.adapters.capture.hotkeys import LiveControlState
from cs2_vision_access.adapters.capture.live_pipeline.config import (
    LivePipelineConfig,
    LivePipelineError,
    LiveRunSummary,
)
from cs2_vision_access.adapters.capture.live_pipeline.diagnostics import (
    _Diagnostics,
    _percentile,
)
from cs2_vision_access.adapters.capture.live_pipeline.frame import _create_renderer, run_frame
from cs2_vision_access.adapters.capture.manager import CaptureManager
from cs2_vision_access.adapters.models.segmenters import Segmenter
from cs2_vision_access.adapters.rendering.renderer import OutlineStyle
from cs2_vision_access.application.live.inference.temporal import SuppressOnlyTemporalPolicy

_DIAGNOSTIC_PRINT_INTERVAL = 5.0


def run_live_pipeline(
    *,
    segmenter: Segmenter,
    config: LivePipelineConfig | None = None,
    outline_style: OutlineStyle | None = None,
    renderer: Any = None,
    on_frame: Callable[[np.ndarray, int, int], None] | None = None,
) -> LiveRunSummary:
    cfg = config or LivePipelineConfig()
    try:
        import cv2
    except ImportError as error:
        raise LivePipelineError(
            "OpenCV is required for live capture; install project dependencies"
        ) from error

    capture_mgr = CaptureManager(
        cfg.capture,
        cv2,
        max_frame_pixels=cfg.max_frame_pixels,
        max_consecutive_read_failures=cfg.max_consecutive_read_failures,
    )
    capture_mgr.open()
    cap_info = capture_mgr.info
    actual_width = cap_info.width
    actual_height = cap_info.height
    capture_fps = cap_info.fps

    base_outline_style = outline_style or OutlineStyle()

    if renderer is None:
        renderer = _create_renderer(
            cfg.alpha_output_mode, cfg.enable_alpha_fill, base_outline_style
        )

    temporal_policy: SuppressOnlyTemporalPolicy | None = (
        SuppressOnlyTemporalPolicy(cfg.temporal_config)
        if cfg.temporal_config is not None and cfg.temporal_config.enabled
        else None
    )

    display_scale = max(0.1, min(1.0, cfg.display_scale))
    display_width = int(actual_width * display_scale) if not cfg.headless else 0
    display_height = int(actual_height * display_scale) if not cfg.headless else 0

    control_state = cfg.control_state or LiveControlState()
    control_state.output_mode = cfg.alpha_output_mode
    control_state.alpha_fill = cfg.enable_alpha_fill
    control_state.temporal_enabled = cfg.temporal_config is not None and cfg.temporal_config.enabled

    last_temporal_enabled = control_state.temporal_enabled
    last_output_mode = control_state.output_mode
    last_alpha_fill = control_state.alpha_fill

    display_mgr = DisplayManager(
        cv2_module=cv2,
        window_name="CS2 Vision Access - Live",
        display_width=display_width,
        display_height=display_height,
        overlay_window=cfg.overlay_window,
        enable_hotkeys=cfg.enable_hotkeys,
        control_state=control_state,
    )
    display_mgr.open(actual_width, actual_height)

    frame_index = 0
    frames_inferred_total = 0
    capture_age_dropped = 0
    instances_predicted_total = 0
    instances_outlined_total = 0
    temporal_suppressed_total = 0
    diag = _Diagnostics()
    started = time.perf_counter()
    termination_reason = "running"
    pause_started: float | None = None

    try:
        while True:
            if cfg.max_frames > 0 and frame_index >= cfg.max_frames:
                termination_reason = "frame_limit"
                break

            if cfg.terminate_event is not None and cfg.terminate_event.is_set():
                termination_reason = "user_stop"
                break

            if not display_mgr.poll_events():
                termination_reason = "user_stop"
                break

            if control_state.paused and not cfg.headless:
                if pause_started is None:
                    pause_started = time.perf_counter()
                if not display_mgr.show_paused(None):
                    termination_reason = "user_stop"
                    break
                if not control_state.paused:
                    diag.mark_pause(time.perf_counter() - pause_started)
                    pause_started = None
                frame_index += 1
                continue

            result = run_frame(
                frame_index=frame_index,
                capture_mgr=capture_mgr,
                segmenter=segmenter,
                cfg=cfg,
                capture_fps=capture_fps,
                started=started,
                base_outline_style=base_outline_style,
                renderer=renderer,
                temporal_policy=temporal_policy,
                control_state=control_state,
                display_mgr=display_mgr,
                on_frame=on_frame,
                diag=diag,
                last_temporal_enabled=last_temporal_enabled,
                last_output_mode=last_output_mode,
                last_alpha_fill=last_alpha_fill,
            )

            if result.termination_reason is not None:
                termination_reason = result.termination_reason
                break

            # Retain hotkey-rebound objects for the next frame.
            if result.renderer is not None:
                renderer = result.renderer
            temporal_policy = result.temporal_policy
            capture_age_dropped += result.frames_dropped
            frames_inferred_total += result.frames_inferred_delta
            instances_predicted_total += result.instances_predicted_delta
            instances_outlined_total += result.instances_outlined_delta
            temporal_suppressed_total += result.temporal_suppressed_delta
            last_temporal_enabled = result.last_temporal_enabled
            last_output_mode = result.last_output_mode
            last_alpha_fill = result.last_alpha_fill
            frame_index += 1

    except KeyboardInterrupt:
        termination_reason = "user_stop"
    finally:
        capture_mgr.close()
        if cfg.output_sink is not None:
            cfg.output_sink.close()
        display_mgr.close()

    elapsed = time.perf_counter() - started
    throughput_fps = frame_index / elapsed if elapsed > 0 else 0.0

    return LiveRunSummary(
        frames_processed=frame_index,
        frames_inferred=frames_inferred_total,
        frames_dropped=frame_index - frames_inferred_total,
        instances_predicted=instances_predicted_total,
        instances_outlined=instances_outlined_total,
        temporal_suppressed=temporal_suppressed_total,
        inference_ms_p50=_percentile(diag.inference_ms, 50),
        inference_ms_p95=_percentile(diag.inference_ms, 95),
        pipeline_ms_p50=_percentile(diag.pipeline_ms, 50),
        pipeline_ms_p95=_percentile(diag.pipeline_ms, 95),
        elapsed_seconds=elapsed,
        capture_fps=capture_fps,
        throughput_fps=throughput_fps,
        termination_reason=termination_reason,
        capture_age_dropped=capture_age_dropped,
    )
