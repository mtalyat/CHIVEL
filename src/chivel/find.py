from __future__ import annotations

import re
import time
from typing import List, Optional, Pattern, Sequence, Union

import cv2
import numpy as np

from .capture import DISPLAY_COUNT, capture, get_primary_display
from .core import Image, Match, Rect
from .input import _check_abort_key

_ocr_engine = None
TextSearch = Union[str, Pattern[str]]
SearchItem = Union[TextSearch, Image]

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


def _normalize_text_key(value: str) -> str:
    return " ".join(value.strip().lower().split())


def _rect_iou(a: Rect, b: Rect) -> float:
    ax1, ay1 = a.x, a.y
    ax2, ay2 = a.x + a.width, a.y + a.height
    bx1, by1 = b.x, b.y
    bx2, by2 = b.x + b.width, b.y + b.height

    ix1 = max(ax1, bx1)
    iy1 = max(ay1, by1)
    ix2 = min(ax2, bx2)
    iy2 = min(ay2, by2)
    iw = max(0, ix2 - ix1)
    ih = max(0, iy2 - iy1)
    inter = float(iw * ih)

    area_a = float(max(0, a.width) * max(0, a.height))
    area_b = float(max(0, b.width) * max(0, b.height))
    denom = area_a + area_b - inter
    if denom <= 0:
        return 0.0
    return inter / denom


def _make_ocr_passes(arr_bgr: np.ndarray) -> List[np.ndarray]:
    # Multiple passes improve OCR recall for mixed contrast and anti-aliased UI text.
    raw = arr_bgr

    norm = np.empty_like(arr_bgr)
    cv2.normalize(arr_bgr, norm, alpha=0, beta=255, norm_type=cv2.NORM_MINMAX)
    norm = cv2.convertScaleAbs(norm, alpha=1.2, beta=0)

    sharp = np.empty_like(arr_bgr)
    cv2.normalize(arr_bgr, sharp, alpha=0, beta=255, norm_type=cv2.NORM_MINMAX)
    sharp = cv2.convertScaleAbs(sharp, alpha=1.35, beta=0)
    kernel = np.array([[0, -1, 0], [-1, 6, -1], [0, -1, 0]], dtype=np.float32)
    sharp = cv2.filter2D(sharp, -1, kernel)

    gray = cv2.cvtColor(arr_bgr, cv2.COLOR_BGR2GRAY)
    gray_norm = np.empty_like(gray)
    cv2.normalize(gray, gray_norm, alpha=0, beta=255, norm_type=cv2.NORM_MINMAX)
    gray = gray_norm
    _, bw = cv2.threshold(gray, 145, 255, cv2.THRESH_BINARY)
    bw = cv2.cvtColor(bw, cv2.COLOR_GRAY2BGR)

    return [raw, norm, sharp, bw]


def _dedupe_text_matches(matches: List[Match], iou_threshold: float = 0.65) -> List[Match]:
    kept: List[Match] = []
    for match in sorted(matches, key=lambda m: m.rect.width * m.rect.height):
        label = (match.label or "").strip()
        if not label:
            continue
        label_key = _normalize_text_key(label)
        if not label_key:
            continue

        duplicate = False
        for existing in kept:
            if _normalize_text_key(existing.label or "") != label_key:
                continue
            if _rect_iou(match.rect, existing.rect) >= iou_threshold:
                duplicate = True
                break
        if not duplicate:
            kept.append(match)
    return kept


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

    passes = _make_ocr_passes(arr)

    pattern: Pattern[str] | None = search if isinstance(search, re.Pattern) else None
    needle = search.lower() if isinstance(search, str) else None
    all_matches: List[Match] = []

    for pass_arr in passes:
        results, _ = engine(pass_arr)
        if not results:
            continue
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
            all_matches.append(Match(Rect(x, y, w, h), label=text_value))

    if not all_matches:
        return []
    return _dedupe_text_matches(all_matches)


def _search_one(source: Image, item: SearchItem, threshold: float) -> List[Match]:
    if isinstance(item, str) or isinstance(item, re.Pattern):
        return find_text(source, item, threshold=0.0)
    return find_image(source, item, threshold=threshold)


def _display_search_order(display_indices: Optional[Sequence[int]] = None) -> List[int]:
    if display_indices is not None:
        return [int(display_index) for display_index in display_indices]

    total = int(DISPLAY_COUNT)
    if total <= 0:
        return []

    primary = int(get_primary_display())
    order = [primary]
    order.extend(display_index for display_index in range(total) if display_index != primary)
    return order


def find_any(
    source: Image,
    search: Sequence[SearchItem],
    threshold: float = 0.8,
) -> List[Match]:
    for item in search:
        matches = _search_one(source, item, threshold)
        if matches:
            return matches
    return []


def find_all(
    source: Image,
    search: Sequence[SearchItem],
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
    *search: SearchItem,
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
    *search: SearchItem,
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


def find_display(
    search: SearchItem,
    threshold: float = 0.8,
    display_indices: Optional[Sequence[int]] = None,
) -> int:
    _check_abort_key()
    for display_index in _display_search_order(display_indices):
        source = capture(display_index=display_index)
        matches = _search_one(source, search, threshold)
        if matches:
            return int(display_index)
    return -1


def expect_display(
    search: SearchItem,
    interval: float = 1.0,
    timeout: float = -1.0,
    threshold: float = 0.8,
    display_indices: Optional[Sequence[int]] = None,
) -> int:
    start = time.time()
    while True:
        _check_abort_key()
        found_on = find_display(search, threshold=threshold, display_indices=display_indices)
        if found_on >= 0:
            return found_on

        if timeout >= 0 and (time.time() - start) >= timeout:
            return -1
        time.sleep(interval)
        _check_abort_key()
