from __future__ import annotations

import re
import time
from typing import List, Pattern, Sequence, Union

import cv2
import numpy as np

from .capture import capture
from .core import Image, Match, Rect
from .input import _check_abort_key

_ocr_engine = None
TextSearch = Union[str, Pattern[str]]

def _get_ocr_engine():
    global _ocr_engine
    if _ocr_engine is None:
        try:
            from rapidocr_onnxruntime import RapidOCR
        except ImportError as exc:
            raise RuntimeError(
                "rapidocr-onnxruntime is not installed. Run: pip install rapidocr-onnxruntime"
            ) from exc
        _ocr_engine = RapidOCR()
    return _ocr_engine


def _to_gray(arr: np.ndarray) -> np.ndarray:
    if len(arr.shape) == 2:
        return arr
    return cv2.cvtColor(arr, cv2.COLOR_BGR2GRAY)


def find_image(source: Image, search: Image, threshold: float = 0.8) -> List[Match]:
    source_gray = _to_gray(source.array)
    search_gray = _to_gray(search.array)

    result = cv2.matchTemplate(source_gray, search_gray, cv2.TM_CCOEFF_NORMED)
    ys, xs = np.where(result >= threshold)

    h, w = search_gray.shape[:2]
    matches: List[Match] = []
    for x, y in zip(xs.tolist(), ys.tolist()):
        matches.append(Match(Rect(int(x), int(y), int(w), int(h))))
    return matches


def find_text(source: Image, search: TextSearch, threshold: float = 0.0) -> List[Match]:
    engine = _get_ocr_engine()

    arr = source.array
    if len(arr.shape) == 2:
        arr = cv2.cvtColor(arr, cv2.COLOR_GRAY2BGR)

    results, _ = engine(arr)
    if not results:
        return []

    pattern: Pattern[str] | None = search if isinstance(search, re.Pattern) else None
    needle = search.lower() if isinstance(search, str) else None
    out: List[Match] = []
    for item in results:
        box, text, score = item[0], item[1], float(item[2])
        if score < threshold:
            continue
        text_value = str(text)
        if pattern is not None:
            if pattern.search(text_value) is None:
                continue
        elif needle is not None:
            if needle not in text_value.lower():
                continue
        pts = np.array(box, dtype=np.int32)
        x = int(pts[:, 0].min())
        y = int(pts[:, 1].min())
        w = int(pts[:, 0].max()) - x
        h = int(pts[:, 1].max()) - y
        out.append(Match(Rect(x, y, w, h), label=text_value))
    return out


def _search_one(source: Image, item: Union[TextSearch, Image], threshold: float) -> List[Match]:
    if isinstance(item, str) or isinstance(item, re.Pattern):
        return find_text(source, item, threshold=0.0)
    return find_image(source, item, threshold=threshold)


def find_any(
    source: Image,
    search: Sequence[Union[TextSearch, Image]],
    threshold: float = 0.8,
) -> List[Match]:
    for item in search:
        matches = _search_one(source, item, threshold)
        if matches:
            return matches
    return []


def find_all(
    source: Image,
    search: Sequence[Union[TextSearch, Image]],
    threshold: float = 0.8,
) -> List[Match]:
    all_matches: List[Match] = []
    for item in search:
        matches = _search_one(source, item, threshold)
        if not matches:
            return []
        all_matches.extend(matches)
    return all_matches


def wait(seconds: float) -> None:
    _check_abort_key()
    time.sleep(seconds)
    _check_abort_key()


def expect_any(
    *search: Union[TextSearch, Image],
    interval: float = 1.0,
    timeout: float = -1.0,
    display_index: int = 0,
    threshold: float = 0.8,
) -> List[Match]:
    start = time.time()
    while True:
        _check_abort_key()
        source = capture(display_index=display_index)
        matches = find_any(source, list(search), threshold=threshold)
        if matches:
            return matches

        if timeout >= 0 and (time.time() - start) >= timeout:
            return []
        time.sleep(interval)
        _check_abort_key()


def expect_all(
    *search: Union[TextSearch, Image],
    interval: float = 1.0,
    timeout: float = -1.0,
    display_index: int = 0,
    threshold: float = 0.8,
) -> List[Match]:
    start = time.time()
    while True:
        _check_abort_key()
        source = capture(display_index=display_index)
        matches = find_all(source, list(search), threshold=threshold)
        if matches:
            return matches

        if timeout >= 0 and (time.time() - start) >= timeout:
            return []
        time.sleep(interval)
        _check_abort_key()
