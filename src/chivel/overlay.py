from __future__ import annotations

import ctypes
import os
import time
from typing import Callable, Iterable, Optional, Tuple, Union

from .capture import display_get_rect, get_primary_display
from .core import Color, Match, Point, Rect
from .input import _check_abort_key, mouse_get_location_global

PointLike = Union[Point, Tuple[int, int]]


class ScreenOverlay:
    """Transparent click-through overlay window for live drawing."""

    def __init__(self, display_index: Optional[int] = None, click_through: bool = True) -> None:
        if os.name != "nt":
            raise NotImplementedError("ScreenOverlay is currently supported on Windows only")

        try:
            import tkinter as tk
        except Exception as exc:
            raise RuntimeError("tkinter is required for ScreenOverlay") from exc

        self._tk = tk
        self.display_index = get_primary_display() if display_index is None else int(display_index)
        self.display_rect = display_get_rect(self.display_index)

        self._root = tk.Tk()
        self._root.overrideredirect(True)
        self._root.attributes("-topmost", True)
        self._root.configure(bg="black")
        self._root.wm_attributes("-transparentcolor", "black")
        self._root.geometry(
            f"{self.display_rect.width}x{self.display_rect.height}+{self.display_rect.x}+{self.display_rect.y}"
        )

        self._canvas = tk.Canvas(
            self._root,
            width=self.display_rect.width,
            height=self.display_rect.height,
            bg="black",
            highlightthickness=0,
            bd=0,
        )
        self._canvas.pack(fill="both", expand=True)

        self._closed = False
        self.present()

        if click_through:
            self._set_click_through()

    @staticmethod
    def _to_hex(color: Optional[Color]) -> str:
        c = color or Color(0, 255, 0)
        return f"#{int(c.r):02x}{int(c.g):02x}{int(c.b):02x}"

    @staticmethod
    def _to_xy(point: PointLike) -> Tuple[int, int]:
        if isinstance(point, Point):
            return int(point.x), int(point.y)
        return int(point[0]), int(point[1])

    def _set_click_through(self) -> None:
        GWL_EXSTYLE = -20
        WS_EX_LAYERED = 0x00080000
        WS_EX_TRANSPARENT = 0x00000020
        WS_EX_TOOLWINDOW = 0x00000080
        LWA_COLORKEY = 0x00000001

        hwnd = int(self._root.winfo_id())
        user32 = ctypes.windll.user32
        black_colorkey = 0x000000
        style = user32.GetWindowLongW(hwnd, GWL_EXSTYLE)
        style |= WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW
        user32.SetWindowLongW(hwnd, GWL_EXSTYLE, style)
        # Re-apply explicit color-key transparency. Without this, some systems keep
        # the layered window fully opaque and the black background becomes visible.
        user32.SetLayeredWindowAttributes(hwnd, black_colorkey, 255, LWA_COLORKEY)

    def clear(self) -> None:
        self._canvas.delete("all")

    def draw_rect(
        self,
        rect: Rect,
        color: Optional[Color] = None,
        thickness: int = 2,
        label: Optional[str] = None,
    ) -> None:
        c = self._to_hex(color)
        self._canvas.create_rectangle(
            rect.x,
            rect.y,
            rect.x + rect.width,
            rect.y + rect.height,
            outline=c,
            width=max(1, int(thickness)),
        )
        if label:
            self._canvas.create_text(
                rect.x + 4,
                max(8, rect.y - 8),
                text=label,
                anchor="w",
                fill=c,
                font=("Segoe UI", 10, "bold"),
            )

    def draw_match(self, match: Match, color: Optional[Color] = None, thickness: int = 2) -> None:
        self.draw_rect(match.rect, color=color, thickness=thickness, label=match.label)

    def draw_matches(self, matches: Iterable[Match], color: Optional[Color] = None, thickness: int = 2) -> None:
        for match in matches:
            self.draw_match(match, color=color, thickness=thickness)

    def draw_line(self, start: PointLike, end: PointLike, color: Optional[Color] = None, thickness: int = 2) -> None:
        c = self._to_hex(color)
        sx, sy = self._to_xy(start)
        ex, ey = self._to_xy(end)
        self._canvas.create_line(sx, sy, ex, ey, fill=c, width=max(1, int(thickness)))

    def draw_text(self, text: str, pos: PointLike, color: Optional[Color] = None, size: int = 16) -> None:
        c = self._to_hex(color)
        x, y = self._to_xy(pos)
        self._canvas.create_text(x, y, text=text, anchor="nw", fill=c, font=("Segoe UI", max(8, int(size)), "bold"))

    def draw_ellipse(
        self,
        center: Point,
        radius: Union[int, Tuple[int, int]],
        color: Optional[Color] = None,
        thickness: int = 2,
    ) -> None:
        rx, ry = (int(radius), int(radius)) if isinstance(radius, int) else (int(radius[0]), int(radius[1]))
        c = self._to_hex(color)
        self._canvas.create_oval(
            center.x - rx,
            center.y - ry,
            center.x + rx,
            center.y + ry,
            outline=c,
            width=max(1, int(thickness)),
        )

    def draw_crosshair(self, pos: PointLike, size: int = 20, color: Optional[Color] = None, thickness: int = 2) -> None:
        x, y = self._to_xy(pos)
        self.draw_line((x - size, y), (x + size, y), color=color, thickness=thickness)
        self.draw_line((x, y - size), (x, y + size), color=color, thickness=thickness)

    def mouse_point_local(self) -> Optional[Point]:
        point, display_index = mouse_get_location_global()
        if display_index != self.display_index:
            return None
        return Point(point.x - self.display_rect.x, point.y - self.display_rect.y)

    def present(self) -> None:
        if self._closed:
            return
        self._root.update_idletasks()
        self._root.update()

    def close(self) -> None:
        if self._closed:
            return
        self._closed = True
        self._root.destroy()

    def run(
        self,
        updater: Optional[Callable[["ScreenOverlay", int, float], None]] = None,
        fps: float = 60.0,
        duration: float = -1.0,
    ) -> None:
        frame = 0
        start = time.time()
        min_frame = 1.0 / max(1.0, float(fps))

        while True:
            _check_abort_key()
            elapsed = time.time() - start
            if duration >= 0 and elapsed >= duration:
                break

            frame_start = time.time()
            if updater is not None:
                updater(self, frame, elapsed)
            self.present()
            frame += 1

            step = time.time() - frame_start
            sleep_for = min_frame - step
            if sleep_for > 0:
                time.sleep(sleep_for)

        self.close()

    def __enter__(self) -> "ScreenOverlay":
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()


def overlay(display_index: Optional[int] = None, click_through: bool = True) -> ScreenOverlay:
    return ScreenOverlay(display_index=display_index, click_through=click_through)
