"""Fullscreen presenter backends for the second-device composite.

The primary contour topology renders on a second Windows machine: the gaming
PC's HDMI output is captured, segmented, and composited, and the result is
shown fullscreen. This module wraps that presentation surface.

``Win32FullscreenPresenter`` is the real backend: borderless popup window,
per-pixel alpha via ``UpdateLayeredWindow``, topmost, cursor hidden over the
window, optional capture exclusion (streamproof). The pure geometry checks —
native-resolution blitting, capture/present size agreement — live in
``resolve_present_size`` so they can be tested without a display.
"""

from __future__ import annotations

import ctypes
import sys
from typing import Any

import numpy as np

from cs2_vision_access.adapters.capture.presentation import resolve_present_size


class PresenterUnavailable(RuntimeError):
    """No presenter backend for this platform."""


def _last_error() -> int:
    """LastError from ctypes, masked to a Win32 DWORD."""
    return int(ctypes.get_last_error()) & 0xFFFF_FFFF  # type: ignore[attr-defined]


def _require_win32() -> dict[str, Any]:
    """Load the Win32 bindings shared with the overlay backend."""
    from cs2_vision_access.adapters.capture.overlay_backends.win32 import _load_win32

    bindings = _load_win32()
    if bindings is None:
        raise PresenterUnavailable("win32 bindings unavailable")
    return bindings


class Win32FullscreenPresenter:
    """Present composited frames fullscreen, native pixels, no chrome."""

    def __init__(
        self,
        title: str = "Periscope Contour",
        *,
        streamproof: bool = False,
    ) -> None:
        self._title = title
        self._streamproof = streamproof
        self._window: int | None = None
        self._mem_dc: int | None = None
        self._dib: int | None = None
        self._bits: int | None = None
        self._old_obj: int | None = None
        self._width = 0
        self._height = 0
        self._bindings: dict[str, Any] | None = None

    def open(self, width: int, height: int, *, x: int = 0, y: int = 0) -> None:
        """Create the fullscreen window for the target monitor."""
        b = _require_win32()
        self._bindings = b
        self._width = int(width)
        self._height = int(height)
        hinstance = b["GetModuleHandleW"](None)
        wc = b["WNDCLASSW"]()
        wc.lpfnWndProc = b["DefWindowProcW"]
        wc.lpszClassName = "PeriscopeContourPresenter"
        wc.hInstance = hinstance
        b["RegisterClassW"](ctypes.byref(wc))

        # WS_POPUP: no chrome at all. SW_SHOWNOACTIVATE keeps the compositor
        # from stealing focus when it comes up alongside the game.
        ex_style = b["WS_EX_TOPMOST"] | b["WS_EX_LAYERED"] | b["WS_EX_NOACTIVATE"]
        hwnd = b["CreateWindowExW"](
            ex_style,
            "PeriscopeContourPresenter",
            self._title,
            b["WS_POPUP"],
            x,
            y,
            self._width,
            self._height,
            None,
            None,
            hinstance,
            None,
        )
        if not hwnd:
            raise RuntimeError(f"CreateWindowExW failed (winerror={_last_error()})")
        self._window = hwnd

        screen_dc = b["GetDC"](None)
        try:
            self._mem_dc = b["CreateCompatibleDC"](screen_dc)
        finally:
            b["ReleaseDC"](None, screen_dc)

        bmi = b["BITMAPINFO"]()
        bmi.bmiHeader.biSize = ctypes.sizeof(b["BITMAPINFOHEADER"])
        bmi.bmiHeader.biWidth = self._width
        bmi.bmiHeader.biHeight = -self._height  # top-down
        bmi.bmiHeader.biPlanes = 1
        bmi.bmiHeader.biBitCount = 32
        bmi.bmiHeader.biCompression = 0
        bits = ctypes.c_void_p()
        self._dib = b["CreateDIBSection"](
            self._mem_dc, ctypes.byref(bmi), b["DIB_RGB_COLORS"], ctypes.byref(bits), None, 0
        )
        self._bits = bits.value
        self._old_obj = b["SelectObject"](self._mem_dc, self._dib)

        b["SetWindowPos"](
            hwnd,
            b["HWND_TOPMOST"],
            x,
            y,
            self._width,
            self._height,
            b["SWP_NOACTIVATE"] | b["SWP_SHOWWINDOW"],
        )
        if self._streamproof:
            self._apply_display_affinity()

    def _apply_display_affinity(self) -> None:
        """Exclude the presenter window from capture (OBS / game capture)."""
        b = self._bindings
        if b is None or self._window is None:
            return
        wda_excludefromcapture = 1 << 4  # WDA_EXCLUDEFROMCAPTURE, Win10 2004+
        if "SetWindowDisplayAffinity" in b:
            b["SetWindowDisplayAffinity"](self._window, wda_excludefromcapture)

    def show_frame(self, frame_rgba: np.ndarray) -> None:
        """Blit a native-resolution RGBA frame onto the presenter surface.

        The geometry check is the pipeline's 1:1 contract: a composited frame
        that does not match the presenter resolution is a configuration error,
        not something to scale over.
        """
        if self._window is None or self._bits is None:
            return
        h, w = frame_rgba.shape[:2]
        resolve_present_size(w, h, self._width, self._height)
        b = _require_win32()
        if frame_rgba.ndim != 3 or frame_rgba.shape[2] != 4:
            raise ValueError("presenter expects RGBA frames of shape (H, W, 4)")
        frame = np.ascontiguousarray(frame_rgba, dtype=np.uint8)
        frame = np.ascontiguousarray(frame_rgba, dtype=np.uint8)
        bgra = np.empty_like(frame)
        bgra[:, :, 0] = frame[:, :, 2]
        bgra[:, :, 1] = frame[:, :, 1]
        bgra[:, :, 2] = frame[:, :, 0]
        bgra[:, :, 3] = frame[:, :, 3]
        ctypes.memmove(self._bits, bgra.ctypes.data, int(bgra.nbytes))

        pt_dst = b["POINT"](0, 0)
        size = b["SIZE"](self._width, self._height)
        pt_src = b["POINT"](0, 0)
        blend = b["BLENDFUNCTION"](0, 0, 255, 1)  # AC_SRC_OVER, alpha
        b["UpdateLayeredWindow"](
            self._window,
            None,
            ctypes.byref(pt_dst),
            ctypes.byref(size),
            self._mem_dc,
            ctypes.byref(pt_src),
            0,
            ctypes.byref(blend),
            b["ULW_ALPHA"],
        )

    def poll_events(self) -> bool:
        """Pump messages. Returns False when the window is gone."""
        if self._window is None or self._bindings is None:
            return False
        msg = self._bindings["MSG"]()
        while self._bindings["PeekMessageW"](
            ctypes.byref(msg), None, 0, 0, self._bindings["PM_REMOVE"]
        ):
            self._bindings["TranslateMessageW"](ctypes.byref(msg))
            self._bindings["DispatchMessageW"](ctypes.byref(msg))
        return True

    def close(self) -> None:
        """Release the window and its GDI objects."""
        b: dict[str, Any] | None
        try:
            b = _require_win32()
        except PresenterUnavailable:
            b = self._bindings
        if self._window is not None and b is not None:
            b.get("DestroyWindow", lambda *a: None)(self._window)
        self._window = None
        self._mem_dc = None
        self._dib = None
        self._bits = None
        self._old_obj = None


def create_fullscreen_presenter(
    width: int,
    height: int,
    *,
    title: str = "Periscope Contour",
    streamproof: bool = False,
) -> Any:
    """Return the platform presenter, raising on unsupported platforms."""
    if sys.platform != "win32":
        raise PresenterUnavailable(
            f"fullscreen composite is Windows-only; {sys.platform} hosts can "
            "use --output-mode overlay on the gaming machine instead"
        )
    presenter = Win32FullscreenPresenter(title=title, streamproof=streamproof)
    presenter.open(width, height)
    return presenter
