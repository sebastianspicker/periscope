"""live subcommand — real-time HDMI capture with contour overlay or alpha output.

Usage::

    cs2-vision live --input-device 0 \\
        --model artifacts/yolo26n-seg.onnx \\
        --manifest artifacts/yolo26n-seg.model.json \\
        --class-name person \\
        --outline-preset maximum-visibility

    # Alpha-only output for OBS / KM Box compositing
    cs2-vision live --input-device 0 \\
        --model artifacts/yolo26n-seg.onnx \\
        --manifest artifacts/yolo26n-seg.model.json \\
        --class-name person \\
        --output-mode alpha \\
        --headless

    # Green-screen chroma key output
    cs2-vision live --input-device 0 \\
        --model artifacts/yolo26n-seg.onnx \\
        --manifest artifacts/yolo26n-seg.model.json \\
        --class-name person \\
        --output-mode green \\
        --headless

    # Second-device contour composite: shared-memory ring to the presenter
    cs2-vision live --input-device 0 \\
        --model artifacts/yolo26n-seg.onnx \\
        --manifest artifacts/yolo26n-seg.model.json \\
        --output-mode overlay \\
        --output-stream shm:contours \\
        --max-capture-age-ms 40

    # List available capture devices
    cs2-vision live --list-devices
"""

from __future__ import annotations

import argparse
import logging
from pathlib import Path
from typing import Any

from cs2_vision_access.adapters.capture import (
    CaptureConfig,
    FileOutputSink,
    OverlayWindow,
    ScreenCaptureError,
    ScreenCapturer,
    ScreenRegion,
    create_overlay,
    is_overlay_available,
    list_capture_devices,
    list_monitors,
)
from cs2_vision_access.adapters.capture.live_pipeline import (
    LivePipelineConfig,
    LiveRunSummary,
    run_live_pipeline,
)
from cs2_vision_access.adapters.capture.outputs import OutputSink
from cs2_vision_access.adapters.capture.sinks.shared import SharedFrameSink
from cs2_vision_access.adapters.models.segmenters import (
    DEFAULT_SEGMENTER_BACKEND,
    SUPPORTED_SEGMENTER_BACKENDS,
    create_segmenter,
)
from cs2_vision_access.application.configuration import AppConfig, find_config_path, load_config
from cs2_vision_access.application.live.inference.temporal import TemporalStabilityConfig
from cs2_vision_access.interfaces.cli.common import _print_json
from cs2_vision_access.interfaces.cli.inference_args import _parse_segmenter_backend
from cs2_vision_access.interfaces.cli.style_args import _add_style_arguments, _outline_style
from cs2_vision_access.interfaces.cli.types import Subcommands

LOGGER = logging.getLogger(__name__)


def register_live_commands(subcommands: Subcommands) -> None:
    live = subcommands.add_parser(
        "live",
        help="real-time HDMI/SDI capture with configurable outline and alpha output",
        description=(
            "Opens a live capture device (or local video file), runs real-time "
            "inference, and renders configurable outlines. Supports alpha-only "
            "and green-screen output modes for compositing software (OBS, KM Box).\n\n"
            "Press Q or Escape in the display window to stop. Frame skipping "
            "is used automatically when inference cannot keep up with the "
            "capture frame rate to maintain real-time feel."
        ),
    )
    live.add_argument(
        "--config",
        type=Path,
        default=None,
        help=(
            "Path to config JSON file (from cs2-vision setup). CLI flags override config values."
        ),
    )
    live.add_argument(
        "--input-device",
        type=str,
        default=None,
        help=(
            "Capture device index, name substring, or file path. "
            "Use --list-devices to enumerate available devices. "
            "A device index (0, 1, 2, ...) selects by number; "
            "a string matches device names case-insensitively; "
            "a local file path opens a video file for testing. "
            "When omitted, uses the configured device or file source."
        ),
    )
    live.add_argument(
        "--source-type",
        choices=["capture-device", "screen", "file"],
        default=None,
        help="Capture source type; defaults to the configured source type.",
    )
    live.add_argument(
        "--monitor",
        type=int,
        default=None,
        help="1-based monitor index for screen capture; overrides the configured monitor.",
    )
    live.add_argument(
        "--list-devices",
        action="store_true",
        help="List available capture devices and exit",
    )
    live.add_argument(
        "--width",
        type=int,
        default=None,
        help="Preferred capture width in pixels",
    )
    live.add_argument(
        "--height",
        type=int,
        default=None,
        help="Preferred capture height in pixels",
    )
    live.add_argument(
        "--fps",
        type=float,
        default=None,
        help="Preferred capture frame rate",
    )
    live.add_argument(
        "--display-scale",
        type=float,
        default=None,
        help="Display window scale factor (0.1-1.0)",
    )
    live.add_argument(
        "--max-frames",
        type=int,
        default=None,
        help="Stop after N frames (0 = unlimited; defaults to the configured value)",
    )
    live.add_argument(
        "--headless",
        action="store_true",
        default=None,
        help="Run without a display window (for KM Box / streaming integration)",
    )
    live.add_argument(
        "--alpha-only",
        action="store_true",
        default=None,
        help=(
            "Deprecated alias for --output-mode alpha. "
            "Render only outlines with transparent background."
        ),
    )
    live.add_argument(
        "--output-mode",
        choices=["overlay", "alpha", "green"],
        default=None,
        help=(
            "Compositing output mode: overlay (on captured video), "
            "alpha (transparent RGBA background), "
            "green (green-screen chroma key background). "
            "(default: overlay)"
        ),
    )
    live.add_argument(
        "--alpha-fill",
        action="store_true",
        default=None,
        help=(
            "Enable interior fill in alpha/green output mode (off by default for cleaner outlines)"
        ),
    )
    live.add_argument(
        "--output-sink",
        type=Path,
        help="Write rendered output to a video file or PNG sequence directory",
    )
    live.add_argument(
        "--overlay",
        action="store_true",
        default=None,
        help=(
            "use the transparent always-on-top overlay window (platform backend; "
            "falls back to cv2.imshow if unavailable)"
        ),
    )
    live.add_argument(
        "--overlay-x",
        type=int,
        default=0,
        help="Overlay window x position in screen pixels (default: 0 = auto-align)",
    )
    live.add_argument(
        "--overlay-y",
        type=int,
        default=0,
        help="Overlay window y position in screen pixels (default: 0 = auto-align)",
    )
    live.add_argument(
        "--overlay-monitor",
        type=int,
        default=0,
        help="1-based monitor to align the overlay to; 0 = auto (capture region origin)",
    )
    live.add_argument(
        "--backend",
        default=None,
        choices=["auto", "dshow", "avfoundation", "v4l2", "msmf"],
        help="OpenCV capture backend hint (default: auto)",
    )
    live.add_argument(
        "--fourcc",
        default=None,
        help=(
            "Capture codec negotiation (best-effort): MJPG or YUY2. "
            "USB3 capture sticks often require MJPG at high frame rates."
        ),
    )
    live.add_argument(
        "--output-stream",
        default=None,
        metavar="NAME",
        help=(
            "Publish rendered frames to a named shared-memory ring for a "
            "second process (external compositor or presenter)."
        ),
    )
    live.add_argument(
        "--max-capture-age-ms",
        type=float,
        default=None,
        help=(
            "Drop capture frames older than this (milliseconds of capture "
            "time) instead of presenting them late. 0 disables the check."
        ),
    )

    # Manually add inference arguments (avoiding _add_inference_arguments which adds --input)
    live.add_argument("--model", type=Path)
    live.add_argument("--manifest", type=Path)
    supported_backends = ", ".join(sorted(SUPPORTED_SEGMENTER_BACKENDS))
    live.add_argument(
        "--segmenter-backend",
        type=_parse_segmenter_backend,
        default=None,
        help=(
            f"segmentation backend; supported: {supported_backends}; "
            f"default: {DEFAULT_SEGMENTER_BACKEND}"
        ),
    )
    live.add_argument("--class-name", action="append")
    live.add_argument("--confidence", type=float, default=None)
    live.add_argument("--image-size", type=int, default=None)
    live.add_argument("--device", default=None)
    live.add_argument(
        "--temporal-enabled",
        action="store_true",
        default=None,
        help="Suppress outlines until detections are stable across consecutive frames.",
    )
    live.add_argument(
        "--temporal-min-frames",
        type=int,
        default=None,
        help="Consecutive detections required when temporal filtering is enabled.",
    )
    live.add_argument(
        "--temporal-hold",
        action="store_true",
        default=None,
        help="Carry forward stable masks across short detector dropouts (hold-last-mask).",
    )
    live.add_argument(
        "--temporal-max-dropout",
        type=int,
        default=None,
        help=(
            "Max consecutive frames to hold a stable mask when hold-last-mask is "
            "enabled (default 0 = no carry)."
        ),
    )

    # Style args without --prefs and --role-config
    _add_style_arguments(live, include_prefs=False, include_role_config=False)
    live.set_defaults(handler=_handle_live)


def _resolve_capture_source(
    arguments: argparse.Namespace,
    input_config: Any,
    source_type: str,
    width: int,
    height: int,
    fps: float,
    backend: str,
) -> CaptureConfig | ScreenCapturer:
    """Build a ``CaptureConfig`` or ``ScreenCapturer`` from CLI flags and config."""
    if source_type == "screen":
        if arguments.monitor is not None:
            return ScreenCapturer(monitor_index=arguments.monitor)
        if input_config.region is not None:
            return ScreenCapturer(region=ScreenRegion(*input_config.region))
        return ScreenCapturer(monitor_index=input_config.monitor_index)

    if arguments.input_device is not None:
        raw_source = arguments.input_device
    elif source_type == "file":
        raw_source = input_config.file_path
    else:
        raw_source = input_config.device_name or str(input_config.device_index)

    try:
        source: int | str = int(raw_source)
    except ValueError:
        source = raw_source

    return CaptureConfig(
        source=source,
        preferred_width=width,
        preferred_height=height,
        preferred_fps=fps,
        backend=backend,
        fourcc=arguments.fourcc,
    )


def _monitor_origin(monitor_index: int) -> tuple[int, int]:
    """Resolve a 1-based monitor's top-left corner via ``list_monitors``."""
    try:
        for mon in list_monitors():
            if int(mon["index"]) == int(monitor_index):
                return int(mon["left"]), int(mon["top"])
    except (OSError, ScreenCaptureError) as error:
        # Monitor discovery is optional alignment metadata; the overlay still
        # works at its default origin when the display backend is unavailable.
        LOGGER.debug("Could not resolve monitor origin; using (0, 0): %s", error)
    return 0, 0


def _overlay_origin(
    arguments: argparse.Namespace,
    display_config: Any,
    source_type: str | None,
    input_config: Any | None,
) -> tuple[int, int]:
    """Compute the overlay window origin in screen pixels.

    Explicit ``--overlay-x`` / ``--overlay-y`` always win. Otherwise, when the
    source is a screen capture, the overlay aligns to the monitor or region
    being captured so it lines up with the footage. ``--overlay-monitor``
    (or the config value) selects the alignment monitor; 0 = auto.
    """
    overlay_x = getattr(arguments, "overlay_x", 0)
    overlay_y = getattr(arguments, "overlay_y", 0)
    if overlay_x or overlay_y:
        return int(overlay_x), int(overlay_y)

    if (source_type or getattr(arguments, "source_type", None)) != "screen":
        return 0, 0

    overlay_monitor = getattr(arguments, "overlay_monitor", 0)
    if not overlay_monitor:
        overlay_monitor = int(display_config.overlay_monitor)
    if overlay_monitor:
        return _monitor_origin(overlay_monitor)

    monitor_index = getattr(arguments, "monitor", None)
    if monitor_index is not None:
        return _monitor_origin(int(monitor_index))

    if input_config is not None and input_config.region is not None:
        return int(input_config.region[0]), int(input_config.region[1])

    if input_config is not None:
        return _monitor_origin(int(input_config.monitor_index))
    return 0, 0


def _build_overlay(
    arguments: argparse.Namespace,
    display_config: Any,
    headless: bool,
    width: int,
    height: int,
    *,
    source_type: str | None = None,
    input_config: Any | None = None,
) -> OverlayWindow | None:
    """Create an overlay window if requested and available.

    The overlay is positioned with ``--overlay-x`` / ``--overlay-y`` when given,
    otherwise auto-aligned to the captured monitor or region for screen sources.
    """
    use_overlay = arguments.overlay if arguments.overlay is not None else display_config.overlay
    if not use_overlay or headless:
        return None
    x, y = _overlay_origin(arguments, display_config, source_type, input_config)
    if is_overlay_available():
        return create_overlay(
            title=display_config.window_title,
            width=width,
            height=height,
            x=x,
            y=y,
        )
    print("[live] warning: overlay backend not available; using cv2.imshow")
    return None


def _build_segmenter(
    arguments: argparse.Namespace,
    model_config: Any,
) -> Any:
    """Construct a segmenter from CLI flags and config."""
    requested = (
        tuple(arguments.class_name)
        if arguments.class_name is not None
        else model_config.class_names
    )
    return create_segmenter(
        arguments.model or Path(model_config.path),
        arguments.manifest or Path(model_config.manifest),
        backend=(arguments.segmenter_backend or model_config.backend or DEFAULT_SEGMENTER_BACKEND),
        class_names=requested,
        confidence=arguments.confidence
        if arguments.confidence is not None
        else model_config.confidence,
        image_size=arguments.image_size
        if arguments.image_size is not None
        else model_config.image_size,
        device=arguments.device or model_config.device,
    )


def _merge_style_defaults(
    arguments: argparse.Namespace,
    outline_config: Any,
) -> None:
    """Apply config file style values as defaults for unset CLI flags."""
    for attribute, value in (
        ("outline_preset", outline_config.preset),
        ("inner_color", outline_config.inner_color),
        ("outer_color", outline_config.outer_color),
        ("inner_width", outline_config.inner_width),
        ("outer_width", outline_config.outer_width),
        ("fill_opacity", outline_config.fill_opacity),
        ("stroke_pattern", outline_config.stroke_pattern),
        ("dash_period", outline_config.dash_period),
        ("fill_mode", outline_config.fill_mode),
        ("halo_blur", outline_config.halo_blur),
        ("outline_kernel", outline_config.outline_kernel),
    ):
        if getattr(arguments, attribute) is None:
            setattr(arguments, attribute, value)
    if not arguments.adapt_width and outline_config.adapt_width:
        arguments.adapt_width = True
    if not arguments.fixed_widths and outline_config.fixed_widths:
        arguments.fixed_widths = True


def _print_summary(summary: LiveRunSummary) -> None:
    """Print the pipeline run summary as JSON."""
    _print_json(
        {
            "frames_processed": summary.frames_processed,
            "frames_inferred": summary.frames_inferred,
            "frames_dropped": summary.frames_dropped,
            "instances_predicted": summary.instances_predicted,
            "instances_outlined": summary.instances_outlined,
            "inference_ms_p50": round(summary.inference_ms_p50, 2),
            "inference_ms_p95": round(summary.inference_ms_p95, 2),
            "pipeline_ms_p50": round(summary.pipeline_ms_p50, 2),
            "pipeline_ms_p95": round(summary.pipeline_ms_p95, 2),
            "elapsed_seconds": round(summary.elapsed_seconds, 2),
            "capture_fps": round(summary.capture_fps, 1),
            "throughput_fps": round(summary.throughput_fps, 1),
            "termination_reason": summary.termination_reason,
        }
    )


def _handle_live(arguments: argparse.Namespace) -> int:
    if arguments.list_devices:
        devices = list_capture_devices()
        if not devices:
            print("No capture devices found.")
            return 0
        print("Available capture devices:")
        for device in devices:
            print(f"  [{device.index}] {device.name}")
        return 0

    # --- Load configuration ---
    # Use --config path if given, otherwise auto-discover in standard locations
    resolved_config_path = arguments.config if arguments.config is not None else find_config_path()
    config = load_config(resolved_config_path) if resolved_config_path.is_file() else AppConfig()
    input_config = config.input
    model_config = config.model
    outline_config = config.outline
    display_config = config.display

    # --- Resolve capture source ---
    source_type = arguments.source_type or input_config.source_type
    if source_type not in {"capture-device", "screen", "file"}:
        raise ValueError(f"unsupported source type: {source_type!r}")

    width = arguments.width if arguments.width is not None else input_config.width
    height = arguments.height if arguments.height is not None else input_config.height
    fps = arguments.fps if arguments.fps is not None else input_config.fps
    backend = arguments.backend if arguments.backend is not None else input_config.backend
    display_scale = (
        arguments.display_scale if arguments.display_scale is not None else display_config.scale
    )
    headless = arguments.headless if arguments.headless is not None else display_config.headless

    capture = _resolve_capture_source(
        arguments, input_config, source_type, width, height, fps, backend
    )

    # --- Resolve output mode ---
    alpha_mode = arguments.output_mode or outline_config.output_mode
    if arguments.alpha_only:
        alpha_mode = "alpha"
    alpha_fill = (
        arguments.alpha_fill if arguments.alpha_fill is not None else outline_config.alpha_fill
    )

    # --- Temporal config ---
    temporal_enabled = (
        arguments.temporal_enabled
        if arguments.temporal_enabled is not None
        else outline_config.temporal_enabled
    )
    temporal_min_frames = (
        arguments.temporal_min_frames
        if arguments.temporal_min_frames is not None
        else outline_config.temporal_min_frames
    )
    temporal_hold = (
        arguments.temporal_hold
        if arguments.temporal_hold is not None
        else outline_config.temporal_hold
    )
    temporal_max_dropout = (
        arguments.temporal_max_dropout
        if arguments.temporal_max_dropout is not None
        else outline_config.temporal_max_dropout
    )

    # --- Overlay window ---
    overlay_window = _build_overlay(
        arguments,
        display_config,
        headless,
        width,
        height,
        source_type=source_type,
        input_config=input_config,
    )

    # --- Auto-switch output mode for overlay ---
    if overlay_window is not None and alpha_mode != "alpha":
        print("[live] --overlay requires alpha output; switching to --output-mode alpha")
        alpha_mode = "alpha"

    # --- Output sinks ---
    # File sink (direct write) and shared-memory ring (cross-process) are
    # independent: a device-2 compositor consumes the ring while the local
    # preview still shows the composite.
    output_sink: OutputSink | None = (
        FileOutputSink(
            str(arguments.output_sink or display_config.output_sink),
            fps=fps,
        )
        if arguments.output_sink is not None or display_config.output_sink is not None
        else None
    )
    stream_name = arguments.output_stream
    if stream_name is not None:
        if not stream_name.startswith("shm:"):
            raise ValueError(f"unsupported --output-stream {stream_name!r}; expected shm:<name>")
        output_sink = SharedFrameSink(
            stream_name[4:],
            width,
            height,
        )

    # --- Build pipeline config ---
    pipeline_config = LivePipelineConfig(
        capture=capture,
        max_frames=(
            arguments.max_frames if arguments.max_frames is not None else display_config.max_frames
        ),
        display_scale=display_scale,
        headless=headless,
        alpha_output_mode=alpha_mode,
        enable_alpha_fill=alpha_fill,
        temporal_config=TemporalStabilityConfig(
            enabled=temporal_enabled,
            min_consecutive_frames=temporal_min_frames,
            hold_last_mask=temporal_hold,
            max_dropout_frames=temporal_max_dropout,
        ),
        output_sink=output_sink,
        overlay_window=overlay_window,
        max_capture_age_ms=(
            arguments.max_capture_age_ms if arguments.max_capture_age_ms is not None else 0.0
        ),
    )

    # --- Style ---
    _merge_style_defaults(arguments, outline_config)
    style = _outline_style(arguments)

    # --- Segmenter ---
    segmenter = _build_segmenter(arguments, model_config)

    # --- Run ---
    summary = run_live_pipeline(
        segmenter=segmenter,
        config=pipeline_config,
        outline_style=style,
    )

    _print_summary(summary)
    return 0
