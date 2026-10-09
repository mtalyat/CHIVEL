from __future__ import annotations

import re
import time
from typing import List, Optional, Pattern, Sequence, Tuple, Union

from .capture import capture, get_primary_display
from .constants import BUTTON_LEFT
from .core import Image, Match, Point, Rect
from .find import find_image, find_text
from .shapes import ShapeTemplate, find_shape
from .input import _check_abort_key, mouse_click, mouse_down, mouse_move, mouse_up

TextSearch = Union[str, Pattern[str]]
PointLike = Union[Point, Rect, Tuple[int, int], Sequence[int]]


class ScreenElement:
    def __init__(self, display_index: Optional[int] = None, search_rect: Optional[Rect] = None) -> None:
        self.display_index = display_index
        self.search_rect = search_rect
        self.last_match: Optional[Match] = None

    def _resolve_display_index(self) -> int:
        if self.display_index is None:
            return get_primary_display()
        return int(self.display_index)

    def _offset_matches(self, matches: List[Match]) -> List[Match]:
        if self.search_rect is None:
            return [Match(Rect(m.rect.x, m.rect.y, m.rect.width, m.rect.height), label=m.label) for m in matches]

        out: List[Match] = []
        for m in matches:
            out.append(
                Match(
                    Rect(
                        self.search_rect.x + m.rect.x,
                        self.search_rect.y + m.rect.y,
                        m.rect.width,
                        m.rect.height,
                    ),
                    label=m.label,
                )
            )
        return out

    def _find_matches(self) -> List[Match]:
        raise NotImplementedError

    def find_all(self) -> List[Match]:
        matches = self._find_matches()
        if matches:
            self.last_match = matches[0]
        return matches

    def find(self) -> Optional[Match]:
        matches = self.find_all()
        return matches[0] if matches else None

    def expect(self, interval: float = 0.2, timeout: float = -1.0) -> bool:
        start = time.time()
        while True:
            _check_abort_key()
            hit = self.find()
            if hit is not None:
                return True
            if timeout >= 0 and (time.time() - start) >= timeout:
                return False
            time.sleep(interval)
            _check_abort_key()

    def exists(self, timeout: float = 0.0, interval: float = 0.2) -> bool:
        if timeout <= 0:
            return self.find() is not None
        return self.expect(interval=interval, timeout=timeout)

    def require(self, interval: float = 0.2, timeout: float = -1.0) -> Match:
        found = self.expect(interval=interval, timeout=timeout)
        if not found or self.last_match is None:
            raise LookupError(f"Element not found: {self}")
        return self.last_match

    def _resolve_match(self, match: Optional[Match] = None) -> Optional[Match]:
        if match is not None:
            self.last_match = match
            return match
        if self.last_match is not None:
            return self.last_match
        return self.find()

    def center(self, match: Optional[Match] = None) -> Optional[Point]:
        resolved = self._resolve_match(match)
        if resolved is None:
            return None
        return resolved.rect.center()

    def move(self, match: Optional[Match] = None) -> bool:
        target = self.center(match)
        if target is None:
            return False
        mouse_move((target.x, target.y), display_index=self._resolve_display_index())
        return True

    def click(
        self,
        button: int = BUTTON_LEFT,
        count: int = 1,
        delay: Optional[float] = None,
        match: Optional[Match] = None,
    ) -> bool:
        if not self.move(match):
            return False
        mouse_click(button=button, count=count, delay=delay)
        return True

    def _resolve_point(self, target: PointLike) -> Point:
        if isinstance(target, Point):
            return Point(int(target.x), int(target.y))
        if isinstance(target, Rect):
            return target.center()
        if isinstance(target, (tuple, list)) and len(target) >= 2:
            return Point(int(target[0]), int(target[1]))
        raise ValueError("target must be Point, Rect, or tuple/list(x, y)")

    def drag_to(
        self,
        target: Union["ScreenElement", PointLike],
        button: int = BUTTON_LEFT,
        hold: float = 0.0,
    ) -> bool:
        source_point = self.center()
        if source_point is None:
            return False

        if isinstance(target, ScreenElement):
            target_point = target.center()
            if target_point is None:
                return False
            target_display_index = target._resolve_display_index()
        else:
            try:
                target_point = self._resolve_point(target)
            except Exception:
                return False
            target_display_index = self._resolve_display_index()

        mouse_move((source_point.x, source_point.y), display_index=self._resolve_display_index())
        mouse_down(button)
        try:
            if hold > 0:
                time.sleep(hold)
            mouse_move((target_point.x, target_point.y), display_index=target_display_index)
        finally:
            mouse_up(button)
        return True


class TextElement(ScreenElement):
    def __init__(
        self,
        search: TextSearch,
        threshold: float = 0.0,
        display_index: Optional[int] = None,
        search_rect: Optional[Rect] = None,
    ) -> None:
        super().__init__(display_index=display_index, search_rect=search_rect)
        self.search = search
        self.threshold = float(threshold)

    def _find_matches(self) -> List[Match]:
        source = capture(display_index=self._resolve_display_index(), rect=self.search_rect)
        matches = find_text(source, self.search, threshold=self.threshold)
        return self._offset_matches(matches)


class ImageElement(ScreenElement):
    def __init__(
        self,
        image: Image,
        threshold: float = 0.8,
        display_index: Optional[int] = None,
        search_rect: Optional[Rect] = None,
    ) -> None:
        super().__init__(display_index=display_index, search_rect=search_rect)
        self.image = image
        self.threshold = float(threshold)

    def _find_matches(self) -> List[Match]:
        source = capture(display_index=self._resolve_display_index(), rect=self.search_rect)
        matches = find_image(source, self.image, threshold=self.threshold)
        return self._offset_matches(matches)


class RegionElement(ScreenElement):
    def __init__(self, rect: Rect, display_index: Optional[int] = None) -> None:
        super().__init__(display_index=display_index, search_rect=None)
        self.rect = rect

    def _find_matches(self) -> List[Match]:
        return [Match(Rect(self.rect.x, self.rect.y, self.rect.width, self.rect.height))]

    def capture(self) -> Image:
        return capture(display_index=self._resolve_display_index(), rect=self.rect)


class ShapeElement(ScreenElement):
    def __init__(
        self,
        shape: Union[str, ShapeTemplate],
        threshold: float = 0.2,
        min_area: float = 100.0,
        max_results: int = 0,
        display_index: Optional[int] = None,
        search_rect: Optional[Rect] = None,
    ) -> None:
        super().__init__(display_index=display_index, search_rect=search_rect)
        self.shape = shape
        self.threshold = float(threshold)
        self.min_area = float(min_area)
        self.max_results = int(max_results)

    def _find_matches(self) -> List[Match]:
        source = capture(display_index=self._resolve_display_index(), rect=self.search_rect)
        matches = find_shape(
            source,
            shape=self.shape,
            threshold=self.threshold,
            min_area=self.min_area,
            max_results=self.max_results,
        )
        return self._offset_matches(matches)


ElementSearch = Union[TextSearch, Image, ShapeTemplate, ScreenElement]


def find_element(
    search: ElementSearch,
    display_index: Optional[int] = None,
    search_rect: Optional[Rect] = None,
    threshold: float = 0.8,
    shape_threshold: float = 0.2,
    min_area: float = 100.0,
    max_results: int = 0,
) -> Optional[Match]:
    if isinstance(search, ScreenElement):
        return search.find()
    if isinstance(search, Image):
        return ImageElement(
            search,
            threshold=threshold,
            display_index=display_index,
            search_rect=search_rect,
        ).find()
    if isinstance(search, ShapeTemplate):
        return ShapeElement(
            search,
            threshold=shape_threshold,
            min_area=min_area,
            max_results=max_results,
            display_index=display_index,
            search_rect=search_rect,
        ).find()
    if isinstance(search, str) or isinstance(search, re.Pattern):
        return TextElement(
            search,
            threshold=0.0,
            display_index=display_index,
            search_rect=search_rect,
        ).find()
    raise TypeError(f"Unsupported element search type: {type(search)}")
