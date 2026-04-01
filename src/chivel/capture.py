from __future__ import annotations

from mss import mss
import numpy as np
from typing import Optional

from .core import Image, Rect


_PRIMARY_DISPLAY_INDEX = 0


def get_primary_display() -> int:
    return _PRIMARY_DISPLAY_INDEX


def set_primary_display(display_index: int) -> None:
    global _PRIMARY_DISPLAY_INDEX
    idx = int(display_index)
    if idx < 0 or idx >= DISPLAY_COUNT:
        raise IndexError(f"Invalid display index: {display_index}")
    _PRIMARY_DISPLAY_INDEX = idx


def _resolve_display_index(display_index: Optional[int]) -> int:
    if display_index is None:
        return _PRIMARY_DISPLAY_INDEX
    return int(display_index)


def _get_monitor(display_index: Optional[int]) -> dict:
    index = _resolve_display_index(display_index)
    with mss() as sct:
        monitors = sct.monitors[1:]
        if not monitors:
            raise RuntimeError("No displays detected")
        if index < 0 or index >= len(monitors):
            raise IndexError(f"Invalid display index: {display_index}")
        return dict(monitors[index])


def display_get_rect(display_index: Optional[int] = None) -> Rect:
    mon = _get_monitor(display_index)
    return Rect(mon["left"], mon["top"], mon["width"], mon["height"])


def capture(display_index: Optional[int] = None, rect: Rect = None) -> Image:
    mon = _get_monitor(display_index)
    grab_rect = {
        "left": mon["left"],
        "top": mon["top"],
        "width": mon["width"],
        "height": mon["height"],
    }

    if rect is not None:
        grab_rect = {
            "left": mon["left"] + rect.x,
            "top": mon["top"] + rect.y,
            "width": rect.width,
            "height": rect.height,
        }

    with mss() as sct:
        shot = sct.grab(grab_rect)
        arr = np.array(shot)
        # MSS returns BGRA; keep as OpenCV-friendly array.
        return Image.from_array(arr)


with mss() as _sct:
    DISPLAY_COUNT = len(_sct.monitors[1:])
