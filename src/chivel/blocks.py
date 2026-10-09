from __future__ import annotations

from typing import List, Tuple

import cv2
import numpy as np

from .core import Image, Match, Rect


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
    intersection = float(iw * ih)

    area_a = float(max(0, a.width) * max(0, a.height))
    area_b = float(max(0, b.width) * max(0, b.height))
    denom = area_a + area_b - intersection
    if denom <= 0:
        return 0.0
    return intersection / denom


def _extract_block_candidates(gray: np.ndarray) -> List[np.ndarray]:
    edges = cv2.Canny(gray, 40, 120)
    edges = cv2.morphologyEx(edges, cv2.MORPH_CLOSE, np.ones((3, 3), dtype=np.uint8), iterations=1)
    edges = cv2.dilate(edges, np.ones((3, 3), dtype=np.uint8), iterations=1)

    _, otsu = cv2.threshold(gray, 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU)
    adaptive = cv2.adaptiveThreshold(
        gray,
        255,
        cv2.ADAPTIVE_THRESH_GAUSSIAN_C,
        cv2.THRESH_BINARY,
        31,
        2,
    )

    # Bridge long horizontal/vertical UI separators so regions become stable blocks.
    h_bridge = cv2.morphologyEx(adaptive, cv2.MORPH_CLOSE, np.ones((1, 15), dtype=np.uint8), iterations=1)
    v_bridge = cv2.morphologyEx(adaptive, cv2.MORPH_CLOSE, np.ones((15, 1), dtype=np.uint8), iterations=1)
    bridged = cv2.bitwise_or(h_bridge, v_bridge)

    masks = [edges, otsu, cv2.bitwise_not(otsu), adaptive, bridged]

    out: List[np.ndarray] = []
    for mask in masks:
        cleaned = cv2.morphologyEx(mask, cv2.MORPH_OPEN, np.ones((3, 3), dtype=np.uint8), iterations=1)
        cleaned = cv2.morphologyEx(cleaned, cv2.MORPH_CLOSE, np.ones((5, 5), dtype=np.uint8), iterations=1)
        contours_tree, _ = cv2.findContours(cleaned, cv2.RETR_TREE, cv2.CHAIN_APPROX_SIMPLE)
        contours_ext, _ = cv2.findContours(cleaned, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        out.extend(contours_tree)
        out.extend(contours_ext)
    return out


def _extract_color_block_rects(
    bgr: np.ndarray,
    min_area: int,
    min_width: int,
    min_height: int,
    max_area_ratio: float,
    max_color_bins: int,
) -> List[Rect]:
    h, w = bgr.shape[:2]
    full_area = float(h * w)

    # Quantize Lab space so slightly different shades still group together.
    lab = cv2.cvtColor(bgr, cv2.COLOR_BGR2LAB)
    q_l = (lab[:, :, 0] // 12).astype(np.uint16)
    q_a = (lab[:, :, 1] // 16).astype(np.uint16)
    q_b = (lab[:, :, 2] // 16).astype(np.uint16)
    bins = (q_l << 8) | (q_a << 4) | q_b

    labels, counts = np.unique(bins, return_counts=True)
    if labels.size == 0:
        return []

    # Keep only dominant color bins to avoid per-bin connected-components across
    # thousands of tiny shades.
    min_pixels = max(150, int(min_area * 0.35))
    pairs = [(int(label), int(count)) for label, count in zip(labels.tolist(), counts.tolist()) if int(count) >= min_pixels]
    pairs.sort(key=lambda row: row[1], reverse=True)
    pairs = pairs[: max(1, int(max_color_bins))]

    rects: List[Rect] = []
    for label, _ in pairs:
        mask = np.where(bins == label, 255, 0).astype(np.uint8)
        # Close small gaps in large same-color regions.
        mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, np.ones((5, 5), dtype=np.uint8), iterations=1)
        count, _, stats, _ = cv2.connectedComponentsWithStats(mask, connectivity=8)
        for idx in range(1, int(count)):
            area = int(stats[idx, cv2.CC_STAT_AREA])
            if area < int(min_area):
                continue
            x = int(stats[idx, cv2.CC_STAT_LEFT])
            y = int(stats[idx, cv2.CC_STAT_TOP])
            ww = int(stats[idx, cv2.CC_STAT_WIDTH])
            hh = int(stats[idx, cv2.CC_STAT_HEIGHT])
            if ww < int(min_width) or hh < int(min_height):
                continue
            box_area = float(ww * hh)
            if box_area > float(max_area_ratio) * full_area:
                continue
            fill_ratio = float(area) / box_area
            if fill_ratio < 0.35:
                continue
            # Prefer broader pane-like regions over thin ribbons.
            if min(ww, hh) < 14:
                continue

            # Reject likely global background components that touch most borders.
            touches_left = x <= 1
            touches_top = y <= 1
            touches_right = (x + ww) >= (w - 1)
            touches_bottom = (y + hh) >= (h - 1)
            border_touches = int(touches_left) + int(touches_top) + int(touches_right) + int(touches_bottom)
            if border_touches >= 3 and box_area >= (0.45 * full_area):
                continue

            rects.append(Rect(x, y, ww, hh))
    return rects


def _merge_rects(rects: List[Rect], iou_threshold: float = 0.55, gap_px: int = 8) -> List[Rect]:
    if not rects:
        return []

    merged = list(rects)
    changed = True
    while changed:
        changed = False
        out: List[Rect] = []
        used = [False] * len(merged)
        for i, a in enumerate(merged):
            if used[i]:
                continue
            ax1, ay1 = a.x, a.y
            ax2, ay2 = a.x + a.width, a.y + a.height
            for j in range(i + 1, len(merged)):
                if used[j]:
                    continue
                b = merged[j]
                bx1, by1 = b.x, b.y
                bx2, by2 = b.x + b.width, b.y + b.height
                overlap = _rect_iou(a, b) >= iou_threshold

                # Merge only for strong overlap or near-duplicate containment,
                # not merely spatial proximity (which can chain into full-screen boxes).
                contains = (ax1 <= bx1 and ay1 <= by1 and ax2 >= bx2 and ay2 >= by2) or (
                    bx1 <= ax1 and by1 <= ay1 and bx2 >= ax2 and by2 >= ay2
                )

                if overlap or contains:
                    ax1, ay1 = min(ax1, bx1), min(ay1, by1)
                    ax2, ay2 = max(ax2, bx2), max(ay2, by2)
                    used[j] = True
                    changed = True
            used[i] = True
            out.append(Rect(ax1, ay1, max(1, ax2 - ax1), max(1, ay2 - ay1)))
        merged = out
    return merged


def _cluster_peaks(indices: np.ndarray) -> List[int]:
    if indices.size == 0:
        return []
    groups: List[List[int]] = [[int(indices[0])]]
    for idx in indices[1:]:
        value = int(idx)
        if value - groups[-1][-1] <= 3:
            groups[-1].append(value)
        else:
            groups.append([value])
    return [int(round(sum(group) / len(group))) for group in groups]


def _select_major_lines(
    lines: List[int],
    strengths: np.ndarray,
    limit: int,
    min_gap: int,
    end_value: int,
) -> List[int]:
    if not lines:
        return [0, end_value]

    candidates: List[Tuple[float, int]] = []
    for line in lines:
        idx = max(0, min(int(line), int(strengths.shape[0] - 1)))
        score = float(strengths[idx]) if strengths.size > 0 else 0.0
        candidates.append((score, int(line)))

    candidates.sort(key=lambda row: row[0], reverse=True)
    picked: List[int] = [0, end_value]

    for _, line in candidates:
        if line <= 0 or line >= end_value:
            continue
        if any(abs(line - existing) < int(min_gap) for existing in picked):
            continue
        picked.append(int(line))
        if len(picked) >= int(limit) + 2:
            break

    return sorted(set(int(v) for v in picked))


def _extract_layout_split_rects(
    bgr: np.ndarray,
    min_width: int,
    min_height: int,
    max_area_ratio: float,
) -> List[Rect]:
    h, w = bgr.shape[:2]
    if h < 40 or w < 40:
        return []

    lab = cv2.cvtColor(bgr, cv2.COLOR_BGR2LAB).astype(np.float32)

    col_mean = lab.mean(axis=0)
    row_mean = lab.mean(axis=1)

    col_diff = np.linalg.norm(np.diff(col_mean, axis=0), axis=1)
    row_diff = np.linalg.norm(np.diff(row_mean, axis=0), axis=1)

    # Smooth to stabilize split lines across minor pixel noise.
    if col_diff.size > 3:
        col_diff = cv2.GaussianBlur(col_diff.reshape(1, -1), (1, 9), 0).reshape(-1)
    if row_diff.size > 3:
        row_diff = cv2.GaussianBlur(row_diff.reshape(-1, 1), (9, 1), 0).reshape(-1)

    col_thr = max(float(np.percentile(col_diff, 92)) if col_diff.size else 0.0, float(col_diff.mean() + col_diff.std()) if col_diff.size else 0.0)
    row_thr = max(float(np.percentile(row_diff, 92)) if row_diff.size else 0.0, float(row_diff.mean() + row_diff.std()) if row_diff.size else 0.0)

    col_peaks = np.where(col_diff >= col_thr)[0] if col_diff.size else np.array([], dtype=np.int32)
    row_peaks = np.where(row_diff >= row_thr)[0] if row_diff.size else np.array([], dtype=np.int32)

    x_candidates = _cluster_peaks(col_peaks)
    y_candidates = _cluster_peaks(row_peaks)

    # Pick only strongest and well-spaced boundaries for coarse pane segmentation.
    x_lines = _select_major_lines(x_candidates, col_diff, limit=7, min_gap=max(24, w // 18), end_value=w)
    y_lines = _select_major_lines(y_candidates, row_diff, limit=7, min_gap=max(20, h // 18), end_value=h)

    full_area = float(h * w)
    rects: List[Rect] = []
    for yi in range(len(y_lines) - 1):
        y0 = int(y_lines[yi])
        y1 = int(y_lines[yi + 1])
        hh = y1 - y0
        if hh < int(min_height):
            continue
        for xi in range(len(x_lines) - 1):
            x0 = int(x_lines[xi])
            x1 = int(x_lines[xi + 1])
            ww = x1 - x0
            if ww < int(min_width):
                continue
            area = float(ww * hh)
            if area > float(max_area_ratio) * full_area:
                continue
            rects.append(Rect(x0, y0, ww, hh))

    return rects


def find_rect_blocks(
    source: Image,
    min_area: int = 2000,
    min_width: int = 40,
    min_height: int = 24,
    max_area_ratio: float = 0.92,
    rectangularity_threshold: float = 0.35,
    max_results: int = 80,
    snap_px: int = 4,
    use_color_regions: bool = True,
    use_layout_splits: bool = True,
    detection_scale: float = 1.0,
    max_color_bins: int = 24,
) -> List[Match]:
    """Find rectangular UI-like blocks in an image.

    This is intentionally generic and app-agnostic. It works well for panes,
    sidebars, tab strips, list areas, chat panels, editor regions, etc.
    """

    arr = source.array
    scale = float(detection_scale)
    if scale <= 0:
        scale = 1.0
    if scale > 1.0:
        scale = 1.0

    inv_scale = 1.0
    if scale < 0.999:
        h0, w0 = arr.shape[:2]
        nw = max(1, int(round(w0 * scale)))
        nh = max(1, int(round(h0 * scale)))
        arr = cv2.resize(arr, (nw, nh), interpolation=cv2.INTER_LINEAR)
        inv_scale = 1.0 / scale

    gray = arr if len(arr.shape) == 2 else cv2.cvtColor(arr, cv2.COLOR_BGR2GRAY)
    gray = cv2.GaussianBlur(gray, (3, 3), 0)

    scaled_min_area = max(1, int(round(float(min_area) * scale * scale)))
    scaled_min_width = max(1, int(round(float(min_width) * scale)))
    scaled_min_height = max(1, int(round(float(min_height) * scale)))

    img_h, img_w = gray.shape[:2]
    full_area = float(img_h * img_w)

    contours = _extract_block_candidates(gray)
    rows: List[Tuple[float, Rect]] = []

    for contour in contours:
        contour_area = float(cv2.contourArea(contour))
        if contour_area < float(scaled_min_area):
            continue

        x, y, w, h = cv2.boundingRect(contour)
        if w < int(scaled_min_width) or h < int(scaled_min_height):
            continue

        box_area = float(w * h)
        if box_area <= 0:
            continue

        if box_area > float(max_area_ratio) * full_area:
            continue

        rectangularity = contour_area / box_area
        if rectangularity < float(rectangularity_threshold):
            continue

        perimeter = cv2.arcLength(contour, True)
        if perimeter <= 0:
            continue
        approx = cv2.approxPolyDP(contour, 0.03 * perimeter, True)
        # UI blocks can have small notches/rounded corners; keep modest flexibility.
        if len(approx) < 4 or len(approx) > 12:
            continue

        if snap_px > 1:
            x = int(round(x / snap_px) * snap_px)
            y = int(round(y / snap_px) * snap_px)
            w = int(round(w / snap_px) * snap_px)
            h = int(round(h / snap_px) * snap_px)
            if w < int(scaled_min_width) or h < int(scaled_min_height):
                continue

        rect = Rect(int(x), int(y), int(w), int(h))

        # Lower score is better: favor larger, cleaner rectangles.
        score = (1.0 - min(1.0, rectangularity)) + (1.0 / max(1.0, box_area / 5000.0))
        rows.append((score, rect))

    rows.sort(key=lambda item: item[0])

    picked: List[Tuple[float, Rect]] = []
    for score, rect in rows:
        if any(_rect_iou(rect, keep_rect) >= 0.65 for _, keep_rect in picked):
            continue
        picked.append((score, rect))
        if len(picked) >= int(max_results):
            break

    if use_color_regions and len(arr.shape) == 3:
        color_rects = _extract_color_block_rects(
            arr,
            min_area=max(250, int(scaled_min_area * 0.6)),
            min_width=max(12, int(scaled_min_width * 0.8)),
            min_height=max(10, int(scaled_min_height * 0.8)),
            max_area_ratio=max_area_ratio,
            max_color_bins=max_color_bins,
        )
        for rect in color_rects:
            if any(_rect_iou(rect, keep_rect) >= 0.75 for _, keep_rect in picked):
                continue
            # Color blocks are often pane backgrounds; bias them slightly ahead.
            picked.append((0.05, rect))

    if use_layout_splits and len(arr.shape) == 3:
        layout_rects = _extract_layout_split_rects(
            arr,
            min_width=max(24, int(scaled_min_width)),
            min_height=max(16, int(scaled_min_height)),
            max_area_ratio=max_area_ratio,
        )
        for rect in layout_rects:
            if any(_rect_iou(rect, keep_rect) >= 0.75 for _, keep_rect in picked):
                continue
            # Layout-split rects are stable pane candidates.
            picked.append((0.03, rect))

    merged_rects = _merge_rects([rect for _, rect in picked], iou_threshold=0.62, gap_px=6)
    merged_rects.sort(key=lambda r: float(r.width * r.height), reverse=True)

    if inv_scale != 1.0:
        scaled_rects: List[Rect] = []
        for rect in merged_rects:
            scaled_rects.append(
                Rect(
                    int(round(rect.x * inv_scale)),
                    int(round(rect.y * inv_scale)),
                    max(1, int(round(rect.width * inv_scale))),
                    max(1, int(round(rect.height * inv_scale))),
                )
            )
        merged_rects = scaled_rects

    matches: List[Match] = []
    for idx, rect in enumerate(merged_rects[: int(max_results)], start=1):
        matches.append(Match(rect, label=f"block-{idx}"))
    return matches
