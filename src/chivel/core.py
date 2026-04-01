from __future__ import annotations

from dataclasses import dataclass
from typing import List, Optional, Tuple, Union

import cv2
import numpy as np

from .constants import (
    COLOR_SPACE_BGR,
    COLOR_SPACE_BGRA,
    COLOR_SPACE_GRAY,
    COLOR_SPACE_HSV,
    COLOR_SPACE_RGB,
    COLOR_SPACE_RGBA,
)


@dataclass
class Point:
    x: int
    y: int


@dataclass
class Rect:
    x: int
    y: int
    width: int
    height: int

    def get_position(self) -> Point:
        return Point(self.x, self.y)

    def get_size(self) -> Point:
        return Point(self.width, self.height)


@dataclass
class Match:
    rect: Rect
    label: Optional[str] = None


@dataclass
class Color:
    r: int
    g: int
    b: int
    a: int = 255


def _to_bgr(color: Optional[Color]) -> Tuple[int, int, int]:
    if color is None:
        return (0, 255, 0)
    return (int(color.b), int(color.g), int(color.r))


def _to_draw_color(array: np.ndarray, color: Optional[Color]) -> Union[Tuple[int, int, int], Tuple[int, int, int, int]]:
    bgr = _to_bgr(color)
    if len(array.shape) == 3 and array.shape[2] == 4:
        alpha = 255 if color is None else int(color.a)
        return (bgr[0], bgr[1], bgr[2], alpha)
    return bgr


class Image:
    def __init__(self, width: int, height: int, channels: int = 3) -> None:
        if channels not in (1, 3, 4):
            raise ValueError("channels must be one of 1, 3, or 4")
        shape = (height, width) if channels == 1 else (height, width, channels)
        self.array = np.zeros(shape, dtype=np.uint8)

    @classmethod
    def from_array(cls, array: np.ndarray) -> "Image":
        obj = cls.__new__(cls)
        obj.array = array
        return obj

    def get_size(self) -> Point:
        h, w = self.array.shape[:2]
        return Point(w, h)

    def show(self, window_name: str = "chivel") -> None:
        cv2.imshow(window_name, self.array)
        cv2.waitKey(1)

    def clone(self) -> "Image":
        return Image.from_array(self.array.copy())

    def crop(self, rect: Rect) -> None:
        self.array = self.array[rect.y : rect.y + rect.height, rect.x : rect.x + rect.width].copy()

    def grayscale(self) -> None:
        if len(self.array.shape) == 2:
            return
        self.array = cv2.cvtColor(self.array, cv2.COLOR_BGR2GRAY)

    def scale(self, x: float, y: Optional[float] = None) -> None:
        y = x if y is None else y
        self.array = cv2.resize(self.array, None, fx=x, fy=y, interpolation=cv2.INTER_LINEAR)

    def rotate(self, angle: float) -> None:
        h, w = self.array.shape[:2]
        center = (w / 2.0, h / 2.0)
        matrix = cv2.getRotationMatrix2D(center, angle, 1.0)
        self.array = cv2.warpAffine(self.array, matrix, (w, h))

    def flip(self, flip: int) -> None:
        self.array = cv2.flip(self.array, flip)

    def resize(self, size: Point) -> None:
        self.array = cv2.resize(self.array, (size.x, size.y), interpolation=cv2.INTER_LINEAR)

    def draw_rect(self, rect: Rect, color: Optional[Color] = None, thickness: int = 2) -> None:
        cv2.rectangle(
            self.array,
            (rect.x, rect.y),
            (rect.x + rect.width, rect.y + rect.height),
            _to_draw_color(self.array, color),
            thickness,
        )

    def draw_matches(self, matches: List[Match], color: Optional[Color] = None, thickness: int = 2) -> None:
        for match in matches:
            self.draw_rect(match.rect, color=color, thickness=thickness)
            if match.label:
                self.draw_text(match.label, Point(match.rect.x, max(0, match.rect.y - 5)), color=color)

    def draw_line(self, start: Point, end: Point, color: Optional[Color] = None, thickness: int = 2) -> None:
        cv2.line(self.array, (start.x, start.y), (end.x, end.y), _to_draw_color(self.array, color), thickness)

    def draw_text(
        self,
        text: str,
        pos: Point,
        color: Optional[Color] = None,
        font_size: int = 1,
        thickness: int = 2,
    ) -> None:
        cv2.putText(
            self.array,
            text,
            (pos.x, pos.y),
            cv2.FONT_HERSHEY_SIMPLEX,
            float(font_size),
            _to_draw_color(self.array, color),
            thickness,
            cv2.LINE_AA,
        )

    def draw_ellipse(
        self,
        center: Point,
        radius: Union[int, Tuple[int, int]],
        color: Optional[Color] = None,
        thickness: int = 2,
        angle: float = 0,
    ) -> None:
        axes = (radius, radius) if isinstance(radius, int) else radius
        cv2.ellipse(
            self.array,
            (center.x, center.y),
            axes,
            angle,
            0,
            360,
            _to_draw_color(self.array, color),
            thickness,
        )

    def draw_image(self, image: "Image", pos: Point, alpha: float = 1.0) -> None:
        src = image.array
        h, w = src.shape[:2]
        y0, y1 = pos.y, pos.y + h
        x0, x1 = pos.x, pos.x + w
        roi = self.array[y0:y1, x0:x1]
        if roi.shape[:2] != src.shape[:2]:
            return
        if alpha >= 1.0:
            self.array[y0:y1, x0:x1] = src
            return
        blended = cv2.addWeighted(src, alpha, roi, 1.0 - alpha, 0)
        self.array[y0:y1, x0:x1] = blended

    def invert(self) -> None:
        self.array = cv2.bitwise_not(self.array)

    def brightness(self, value: float) -> None:
        self.array = cv2.convertScaleAbs(self.array, alpha=1.0, beta=value)

    def contrast(self, value: float = 1.0) -> None:
        self.array = cv2.convertScaleAbs(self.array, alpha=value, beta=0)

    def sharpen(self, value: float = 1.0) -> None:
        kernel = np.array([[0, -1, 0], [-1, 5 + value, -1], [0, -1, 0]], dtype=np.float32)
        self.array = cv2.filter2D(self.array, -1, kernel)

    def blur(self, value: int = 3) -> None:
        k = max(1, int(value))
        if k % 2 == 0:
            k += 1
        self.array = cv2.GaussianBlur(self.array, (k, k), 0)

    def threshold(self, threshold: float = 128.0, maxValue: float = 255.0) -> None:
        gray = self.array if len(self.array.shape) == 2 else cv2.cvtColor(self.array, cv2.COLOR_BGR2GRAY)
        _, self.array = cv2.threshold(gray, threshold, maxValue, cv2.THRESH_BINARY)

    def normalize(self, alpha: float = 0.0, beta: float = 255.0) -> None:
        self.array = cv2.normalize(self.array, None, alpha=alpha, beta=beta, norm_type=cv2.NORM_MINMAX)

    def edge(self, threshold1: float = 100.0, threshold2: float = 200.0) -> None:
        gray = self.array if len(self.array.shape) == 2 else cv2.cvtColor(self.array, cv2.COLOR_BGR2GRAY)
        self.array = cv2.Canny(gray, threshold1, threshold2)

    def emboss(self) -> None:
        kernel = np.array([[-2, -1, 0], [-1, 1, 1], [0, 1, 2]], dtype=np.float32)
        self.array = cv2.filter2D(self.array, -1, kernel)

    def split(self) -> List["Image"]:
        if len(self.array.shape) == 2:
            return [Image.from_array(self.array.copy())]
        return [Image.from_array(channel) for channel in cv2.split(self.array)]

    def merge(self, channels: List["Image"]) -> None:
        self.array = cv2.merge([c.array for c in channels])

    def convert(self, color_space: int) -> None:
        mapping = {
            COLOR_SPACE_BGR: None,
            COLOR_SPACE_BGRA: cv2.COLOR_BGR2BGRA,
            COLOR_SPACE_RGB: cv2.COLOR_BGR2RGB,
            COLOR_SPACE_RGBA: cv2.COLOR_BGR2RGBA,
            COLOR_SPACE_GRAY: cv2.COLOR_BGR2GRAY,
            COLOR_SPACE_HSV: cv2.COLOR_BGR2HSV,
        }
        code = mapping.get(color_space)
        if code is None and color_space != COLOR_SPACE_BGR:
            raise ValueError(f"Unsupported color_space: {color_space}")
        if code is not None:
            self.array = cv2.cvtColor(self.array, code)

    def range(self, lower: Color, upper: Color) -> None:
        lo = np.array([lower.b, lower.g, lower.r], dtype=np.uint8)
        hi = np.array([upper.b, upper.g, upper.r], dtype=np.uint8)
        self.array = cv2.inRange(self.array, lo, hi)

    def mask(self, mask: "Image") -> None:
        self.array = cv2.bitwise_and(self.array, self.array, mask=mask.array)
