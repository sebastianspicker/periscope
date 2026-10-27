"""Per-frame processing logic extracted from the live pipeline loop."""

from __future__ import annotations

import time
from collections.abc import Callable
from dataclasses import dataclass
from typing import Any

import numpy as np

from cs2_vision_access.adapters.capture.alpha_renderer import AlphaRenderer, AlphaRendererConfig
from cs2_vision_access.adapters.capture.display import DisplayManager
from cs2_vision_access.adapters.capture.hotkeys import LiveControlState
from cs2_vision_access.adapters.capture.live_pipeline.config import LivePipelineConfig
from cs2_vision_access.adapters.capture.live_pipeline.diagnostics import _Diagnostics
from cs2_vision_access.adapters.models.segmenters import Segmenter
from cs2_vision_access.adapters.rendering.renderer import OutlineRenderer, OutlineStyle
from cs2_vision_access.application.live.inference.temporal import (
    SuppressOnlyTemporalPolicy,
    TemporalStabilityConfig,
)


@dataclass
class _FrameResult:
    termination_reason: str | None = None
    frames_inferred_delta: int = 0
    frames_dropped: int = 0
    instances_predicted_delta: int = 0
    instances_outlined_delta: int = 0
    temporal_suppressed_delta: int = 0
    last_temporal_enabled: bool = False
    last_output_mode: str = "overlay"
    last_alpha_fill: bool = False
    # Rebound control objects must be returned so the runner retains them
    # across frames (hotkey toggles rebind local variables only).
    renderer: AlphaRenderer | OutlineRenderer | None = None
    temporal_policy: SuppressOnlyTemporalPolicy | None = None


def _create_renderer(
    output_mode: str,
    alpha_fill: bool,
    base_style: OutlineStyle,
) -> AlphaRenderer | OutlineRenderer:
    if output_mode != "overlay":
        return AlphaRenderer(
            style=base_style,
            alpha_config=AlphaRendererConfig(
                output_mode=output_mode,
                enable_fill=alpha_fill,
            ),
        )
    return OutlineRenderer(style=base_style)


def run_frame(
    *,
    frame_index: int,
    capture_mgr: Any,
    segmenter: Segmenter,
    cfg: LivePipelineConfig,
    capture_fps: float,
    started: float,
    base_outline_style: OutlineStyle,
    renderer: AlphaRenderer | OutlineRenderer,
    temporal_policy: SuppressOnlyTemporalPolicy | None,
    control_state: LiveControlState,
    display_mgr: DisplayManager,
    on_frame: Callable[[np.ndarray, int, int], None] | None,
    diag: _Diagnostics,
    last_temporal_enabled: bool,
    last_output_mode: str,
    last_alpha_fill: bool,
) -> _FrameResult:

    result = _FrameResult(
        last_temporal_enabled=last_temporal_enabled,
        last_output_mode=last_output_mode,
        last_alpha_fill=last_alpha_fill,
        renderer=renderer,
        temporal_policy=temporal_policy,
    )

    elapsed_since_start = time.perf_counter() - started
    expected_frames = int(elapsed_since_start * capture_fps)
    frames_behind = expected_frames - frame_index

    # Fixed pacing (`max_capture_age_ms` on the config) bounds the queue: drop
    # the frame when it is already older than the consumer-relevant window.
    max_age = getattr(cfg, "max_capture_age_ms", 0.0)
    if max_age > 0 and frames_behind > 0:
        stale_ms = frames_behind * 1000.0 / max(capture_fps, 1.0)
        if stale_ms > max_age:
            capture_mgr.read(frame_index)  # advance past the stale buffer
            result.frames_dropped = 1
            return result

    if frames_behind > cfg.frame_skip_threshold:
        return result

    ok, frame, read_reason = capture_mgr.read(frame_index)
    if not ok:
        if read_reason:
            result.termination_reason = read_reason
        return result

    if frame is None:
        raise RuntimeError("capture returned success without a frame")

    inference_start = time.perf_counter()
    predictions = segmenter.predict(frame, frame_index=frame_index)
    inference_elapsed = (time.perf_counter() - inference_start) * 1000.0
    diag.inference_ms.append(inference_elapsed)

    if control_state.temporal_enabled != result.last_temporal_enabled:
        temporal_policy = (
            SuppressOnlyTemporalPolicy(
                cfg.temporal_config
                if cfg.temporal_config is not None
                else TemporalStabilityConfig(enabled=True)
            )
            if control_state.temporal_enabled
            else None
        )
        result.temporal_policy = temporal_policy
        result.last_temporal_enabled = control_state.temporal_enabled

    temporal_suppressed = 0
    if temporal_policy is not None:
        predictions, temporal_diag = temporal_policy.filter(predictions, frame_index=frame_index)
        temporal_suppressed = temporal_diag.suppressed_count

    current_output_mode = control_state.output_mode
    current_alpha_fill = control_state.alpha_fill

    if (
        current_output_mode != result.last_output_mode
        or current_alpha_fill != result.last_alpha_fill
    ):
        renderer = _create_renderer(current_output_mode, current_alpha_fill, base_outline_style)
        result.renderer = renderer
        result.last_output_mode = current_output_mode
        result.last_alpha_fill = current_alpha_fill

    if hasattr(renderer, "style"):
        renderer.style = control_state.apply_style(base_outline_style)
        if isinstance(renderer, AlphaRenderer):
            renderer._inner_renderer.style = renderer.style

    rendered = renderer.render(frame, predictions, frame_index=frame_index)

    current_frame_preds = [p for p in predictions if p.frame_index == frame_index]

    pipeline_elapsed = (time.perf_counter() - inference_start) * 1000.0
    diag.pipeline_ms.append(pipeline_elapsed)

    if on_frame is not None:
        on_frame(rendered, frame_index, len(predictions))
    if cfg.output_sink is not None:
        cfg.output_sink.send(rendered, frame_index)

    if not cfg.headless and not display_mgr.show_frame(
        rendered, frame_index, diag.inference_ms, len(predictions), started
    ):
        result.termination_reason = "user_stop"
        return result

    diag.maybe_print(
        time.perf_counter(),
        cfg.diagnostic_interval,
        frame_index,
        started,
        frames_behind,
        len(predictions),
        len(current_frame_preds),
    )

    result.frames_inferred_delta = 1
    result.instances_predicted_delta = len(predictions)
    result.instances_outlined_delta = len(current_frame_preds)
    result.temporal_suppressed_delta = temporal_suppressed
    return result
