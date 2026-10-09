from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Callable, Dict, Iterable, List, Optional, Sequence, Tuple, Union

import cv2
import numpy as np

from .core import Image, Match, Rect

Point2D = Tuple[float, float]


@dataclass(frozen=True)
class ShapeTemplate:
    name: str
    contour: np.ndarray


@dataclass(frozen=True)
class _BuiltinShape:
    template: ShapeTemplate
    verifier: Optional[Callable[[np.ndarray], bool]] = None


def _normalize_points(points: Sequence[Point2D], close: bool = True) -> np.ndarray:
    if len(points) < 3:
        raise ValueError("points must contain at least 3 entries")

    pts = np.array([(float(x), float(y)) for x, y in points], dtype=np.float32)
    min_x = float(np.min(pts[:, 0]))
    min_y = float(np.min(pts[:, 1]))
    max_x = float(np.max(pts[:, 0]))
    max_y = float(np.max(pts[:, 1]))

    width = max(1e-6, max_x - min_x)
    height = max(1e-6, max_y - min_y)
    pts[:, 0] = (pts[:, 0] - min_x) / width
    pts[:, 1] = (pts[:, 1] - min_y) / height

    if close and (pts[0, 0] != pts[-1, 0] or pts[0, 1] != pts[-1, 1]):
        pts = np.vstack([pts, pts[0]])

    scaled = np.round(pts * 1000.0).astype(np.int32)
    return scaled.reshape((-1, 1, 2))


def shape_template(name: str, points: Sequence[Point2D], close: bool = True) -> ShapeTemplate:
    return ShapeTemplate(name=name, contour=_normalize_points(points, close=close))


def _normalize_contour(contour: np.ndarray) -> np.ndarray:
    pts = contour.reshape((-1, 2)).astype(np.float32)
    min_x = float(np.min(pts[:, 0]))
    min_y = float(np.min(pts[:, 1]))
    max_x = float(np.max(pts[:, 0]))
    max_y = float(np.max(pts[:, 1]))
    width = max(1e-6, max_x - min_x)
    height = max(1e-6, max_y - min_y)
    pts[:, 0] = (pts[:, 0] - min_x) / width
    pts[:, 1] = (pts[:, 1] - min_y) / height
    scaled = np.round(pts * 1000.0).astype(np.int32)
    return scaled.reshape((-1, 1, 2))


def _template_from_mask(name: str, mask: np.ndarray) -> ShapeTemplate:
    contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    if not contours:
        raise ValueError(f"Could not build template for shape '{name}'")
    contour = max(contours, key=cv2.contourArea)
    return ShapeTemplate(name=name, contour=_normalize_contour(contour))


def _shape_x_template() -> ShapeTemplate:
    mask = np.zeros((256, 256), dtype=np.uint8)
    cv2.line(mask, (32, 32), (224, 224), 255, 28)
    cv2.line(mask, (224, 32), (32, 224), 255, 28)
    return _template_from_mask("x", mask)


def _shape_l_template() -> ShapeTemplate:
    mask = np.zeros((256, 256), dtype=np.uint8)
    cv2.line(mask, (72, 28), (72, 224), 255, 30)
    cv2.line(mask, (72, 224), (224, 224), 255, 30)
    return _template_from_mask("l", mask)


def _shape_circle_points(samples: int = 40) -> List[Point2D]:
    out: List[Point2D] = []
    for i in range(max(12, int(samples))):
        t = (2.0 * math.pi * float(i)) / float(samples)
        out.append((math.cos(t), math.sin(t)))
    return out


def _shape_star_points(outer_radius: float = 1.0, inner_radius: float = 0.45, points: int = 5) -> List[Point2D]:
    out: List[Point2D] = []
    total = max(5, int(points))
    for i in range(total * 2):
        angle = (math.pi / total) * i - (math.pi / 2)
        r = outer_radius if i % 2 == 0 else inner_radius
        out.append((r * math.cos(angle), r * math.sin(angle)))
    return out


def _shape_x_points() -> List[Point2D]:
    # Thick X polygon.
    return [
        (-1.0, -0.7),
        (-0.7, -1.0),
        (0.0, -0.3),
        (0.7, -1.0),
        (1.0, -0.7),
        (0.3, 0.0),
        (1.0, 0.7),
        (0.7, 1.0),
        (0.0, 0.3),
        (-0.7, 1.0),
        (-1.0, 0.7),
        (-0.3, 0.0),
    ]


def _shape_rectangle_points() -> List[Point2D]:
    return [(-1.0, -1.0), (1.0, -1.0), (1.0, 1.0), (-1.0, 1.0)]


def _shape_l_points() -> List[Point2D]:
    return [(-1.0, -1.0), (-0.3, -1.0), (-0.3, 0.3), (1.0, 0.3), (1.0, 1.0), (-1.0, 1.0)]


def _is_circle_like(contour: np.ndarray) -> bool:
    area = float(cv2.contourArea(contour))
    if area <= 0:
        return False
    perimeter = float(cv2.arcLength(contour, True))
    if perimeter <= 0:
        return False
    circularity = (4.0 * math.pi * area) / (perimeter * perimeter)
    return circularity >= 0.65


def _is_rectangle_like(contour: np.ndarray) -> bool:
    peri = cv2.arcLength(contour, True)
    approx = cv2.approxPolyDP(contour, 0.03 * peri, True)
    return len(approx) == 4 and bool(cv2.isContourConvex(approx))


def _is_x_like(contour: np.ndarray) -> bool:
    hull = cv2.convexHull(contour)
    hull_area = float(cv2.contourArea(hull))
    area = float(cv2.contourArea(contour))
    if hull_area <= 0:
        return False
    solidity = area / hull_area
    return solidity <= 0.85


def _is_star_like(contour: np.ndarray) -> bool:
    peri = cv2.arcLength(contour, True)
    approx = cv2.approxPolyDP(contour, 0.03 * peri, True)
    return 8 <= len(approx) <= 14


def _builtin_shapes() -> Dict[str, _BuiltinShape]:
    circle = shape_template("circle", _shape_circle_points())
    rectangle = shape_template("rectangle", _shape_rectangle_points())
    x_shape = _shape_x_template()
    star = shape_template("star", _shape_star_points())
    l_shape = _shape_l_template()

    return {
        "circle": _BuiltinShape(circle, _is_circle_like),
        "rect": _BuiltinShape(rectangle, _is_rectangle_like),
        "rectangle": _BuiltinShape(rectangle, _is_rectangle_like),
        "box": _BuiltinShape(rectangle, _is_rectangle_like),
        "x": _BuiltinShape(x_shape, None),
        "cross": _BuiltinShape(x_shape, None),
        "close": _BuiltinShape(x_shape, None),
        "star": _BuiltinShape(star, _is_star_like),
        "l": _BuiltinShape(l_shape, None),
        "lshape": _BuiltinShape(l_shape, None),
    }


def get_shape_template(shape: Union[str, ShapeTemplate]) -> ShapeTemplate:
    if isinstance(shape, ShapeTemplate):
        return shape
    key = str(shape).strip().lower()
    builtins = _builtin_shapes()
    if key not in builtins:
        raise ValueError(f"Unknown shape '{shape}'. Known shapes: {', '.join(sorted(set(builtins.keys())))}")
    return builtins[key].template


def _get_shape_verifier(shape: Union[str, ShapeTemplate]) -> Optional[Callable[[np.ndarray], bool]]:
    if isinstance(shape, ShapeTemplate):
        return None
    key = str(shape).strip().lower()
    builtins = _builtin_shapes()
    item = builtins.get(key)
    return None if item is None else item.verifier


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


def _line_length(line: np.ndarray) -> float:
    x1, y1, x2, y2 = [float(v) for v in line]
    return math.hypot(x2 - x1, y2 - y1)


def _line_angle_deg(line: np.ndarray) -> float:
    x1, y1, x2, y2 = [float(v) for v in line]
    return math.degrees(math.atan2(y2 - y1, x2 - x1))


def _segment_intersection(line_a: np.ndarray, line_b: np.ndarray) -> Optional[Tuple[float, float]]:
    x1, y1, x2, y2 = [float(v) for v in line_a]
    x3, y3, x4, y4 = [float(v) for v in line_b]

    denom = (x1 - x2) * (y3 - y4) - (y1 - y2) * (x3 - x4)
    if abs(denom) < 1e-6:
        return None

    px = ((x1 * y2 - y1 * x2) * (x3 - x4) - (x1 - x2) * (x3 * y4 - y3 * x4)) / denom
    py = ((x1 * y2 - y1 * x2) * (y3 - y4) - (y1 - y2) * (x3 * y4 - y3 * x4)) / denom

    def _in_range(a: float, b: float, p: float) -> bool:
        lo, hi = (a, b) if a <= b else (b, a)
        return (lo - 3.0) <= p <= (hi + 3.0)

    if not (_in_range(x1, x2, px) and _in_range(y1, y2, py) and _in_range(x3, x4, px) and _in_range(y3, y4, py)):
        return None
    return (px, py)


def _find_x_by_lines(gray: np.ndarray, min_area: float, max_results: int) -> List[Tuple[float, Match]]:
    edges = cv2.Canny(gray, 60, 160)
    lines = cv2.HoughLinesP(edges, 1, np.pi / 180.0, threshold=45, minLineLength=35, maxLineGap=10)
    if lines is None:
        return []
    masks = _shape_component_masks(gray)

    positive: List[np.ndarray] = []
    negative: List[np.ndarray] = []
    for row in lines:
        line = row[0]
        angle = _line_angle_deg(line)
        if abs(angle) < 20 or abs(angle) > 160:
            continue
        if 70 < abs(angle) < 110:
            continue
        if _line_length(line) < 35:
            continue
        if angle > 0:
            positive.append(line)
        else:
            negative.append(line)

    out: List[Tuple[float, Match]] = []
    for a in positive:
        for b in negative:
            ipt = _segment_intersection(a, b)
            if ipt is None:
                continue
            x_vals = [int(a[0]), int(a[2]), int(b[0]), int(b[2])]
            y_vals = [int(a[1]), int(a[3]), int(b[1]), int(b[3])]
            x = min(x_vals)
            y = min(y_vals)
            w = max(x_vals) - x
            h = max(y_vals) - y
            if w <= 0 or h <= 0:
                continue
            if float(w * h) < float(min_area):
                continue

            refined = _component_bbox_at_point(masks, ipt[0], ipt[1], min_area=min_area)
            if refined is not None:
                x, y, w, h = refined.x, refined.y, refined.width, refined.height

            angle_a = abs(_line_angle_deg(a))
            angle_b = abs(_line_angle_deg(b))
            score = abs((angle_a + angle_b) - 90.0) / 90.0
            out.append((float(score), Match(Rect(int(x), int(y), int(w), int(h)), label=f"x:{score:.3f}")))

    out.sort(key=lambda item: item[0])
    deduped: List[Tuple[float, Match]] = []
    for item in out:
        if any(_rect_iou(item[1].rect, keep[1].rect) >= 0.65 for keep in deduped):
            continue
        deduped.append(item)
        if max_results > 0 and len(deduped) >= max_results:
            break
    return deduped


def _point_distance(a: Tuple[float, float], b: Tuple[float, float]) -> float:
    return math.hypot(float(a[0]) - float(b[0]), float(a[1]) - float(b[1]))


def _shape_component_masks(gray: np.ndarray) -> List[np.ndarray]:
    edges = cv2.Canny(gray, 60, 150)
    edges = cv2.morphologyEx(edges, cv2.MORPH_CLOSE, np.ones((3, 3), dtype=np.uint8), iterations=1)
    edges = cv2.dilate(edges, np.ones((3, 3), dtype=np.uint8), iterations=1)

    _, otsu = cv2.threshold(gray, 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU)
    inv_otsu = cv2.bitwise_not(otsu)
    return [edges, otsu, inv_otsu]


def _component_bbox_at_point(masks: Sequence[np.ndarray], x: float, y: float, min_area: float) -> Optional[Rect]:
    best: Optional[Rect] = None
    best_area: Optional[int] = None

    for mask in masks:
        h, w = mask.shape[:2]
        if h == 0 or w == 0:
            continue
        px = max(0, min(w - 1, int(round(x))))
        py = max(0, min(h - 1, int(round(y))))

        binary = np.where(mask > 0, 255, 0).astype(np.uint8)
        count, labels, stats, _ = cv2.connectedComponentsWithStats(binary, connectivity=8)
        if count <= 1:
            continue
        label = int(labels[py, px])
        if label <= 0:
            continue

        area = int(stats[label, cv2.CC_STAT_AREA])
        if area < int(min_area):
            continue

        left = int(stats[label, cv2.CC_STAT_LEFT])
        top = int(stats[label, cv2.CC_STAT_TOP])
        width = int(stats[label, cv2.CC_STAT_WIDTH])
        height = int(stats[label, cv2.CC_STAT_HEIGHT])
        if width <= 0 or height <= 0:
            continue

        # Ignore border-touching components because those are typically
        # background regions in threshold-inverted masks.
        right = left + width
        bottom = top + height
        if left <= 1 or top <= 1 or right >= (w - 1) or bottom >= (h - 1):
            continue

        # Also reject components that are too large to be the target shape.
        if area >= int(0.85 * float(w * h)):
            continue

        # Prefer the smallest valid component around the feature point.
        if best_area is None or area < best_area:
            best_area = area
            best = Rect(left, top, width, height)

    return best


def _find_l_by_lines(gray: np.ndarray, min_area: float, max_results: int) -> List[Tuple[float, Match]]:
    edges = cv2.Canny(gray, 60, 160)
    lines = cv2.HoughLinesP(edges, 1, np.pi / 180.0, threshold=45, minLineLength=30, maxLineGap=10)
    if lines is None:
        return []
    masks = _shape_component_masks(gray)

    vertical: List[np.ndarray] = []
    horizontal: List[np.ndarray] = []
    for row in lines:
        line = row[0]
        angle = abs(_line_angle_deg(line))
        if _line_length(line) < 30:
            continue
        if angle < 20 or angle > 160:
            horizontal.append(line)
        elif 70 < angle < 110:
            vertical.append(line)

    out: List[Tuple[float, Match]] = []
    for v in vertical:
        vp1 = (float(v[0]), float(v[1]))
        vp2 = (float(v[2]), float(v[3]))
        for h in horizontal:
            hp1 = (float(h[0]), float(h[1]))
            hp2 = (float(h[2]), float(h[3]))
            corners = [
                (vp1, hp1),
                (vp1, hp2),
                (vp2, hp1),
                (vp2, hp2),
            ]
            corner_pairs = [(_point_distance(a, b), a, b) for a, b in corners]
            corner_pairs.sort(key=lambda row: row[0])
            dist, ca, cb = corner_pairs[0]
            if dist > 24.0:
                continue

            x_vals = [int(v[0]), int(v[2]), int(h[0]), int(h[2])]
            y_vals = [int(v[1]), int(v[3]), int(h[1]), int(h[3])]
            x = min(x_vals)
            y = min(y_vals)
            w = max(x_vals) - x
            hgt = max(y_vals) - y
            if w <= 0 or hgt <= 0:
                continue
            if float(w * hgt) < float(min_area):
                continue

            corner_x = (ca[0] + cb[0]) * 0.5
            corner_y = (ca[1] + cb[1]) * 0.5
            refined = _component_bbox_at_point(masks, corner_x, corner_y, min_area=min_area)
            if refined is not None:
                x, y, w, hgt = refined.x, refined.y, refined.width, refined.height

            score = dist / max(1.0, min(_line_length(v), _line_length(h)))
            out.append((float(score), Match(Rect(int(x), int(y), int(w), int(hgt)), label=f"l:{score:.3f}")))

    out.sort(key=lambda item: item[0])
    deduped: List[Tuple[float, Match]] = []
    for item in out:
        if any(_rect_iou(item[1].rect, keep[1].rect) >= 0.65 for keep in deduped):
            continue
        deduped.append(item)
        if max_results > 0 and len(deduped) >= max_results:
            break
    return deduped


def find_shape(
    source: Image,
    shape: Union[str, ShapeTemplate],
    threshold: float = 0.2,
    min_area: float = 100.0,
    max_results: int = 0,
) -> List[Match]:
    arr = source.array
    gray = arr if len(arr.shape) == 2 else cv2.cvtColor(arr, cv2.COLOR_BGR2GRAY)
    gray = cv2.GaussianBlur(gray, (5, 5), 0)

    shape_key = str(shape).strip().lower() if isinstance(shape, str) else ""
    if shape_key in {"x", "cross", "close"}:
        specialized = _find_x_by_lines(gray, min_area=float(min_area), max_results=int(max_results))
        if specialized:
            return [row[1] for row in specialized]
    if shape_key in {"l", "lshape"}:
        specialized = _find_l_by_lines(gray, min_area=float(min_area), max_results=int(max_results))
        if specialized:
            return [row[1] for row in specialized]

    h, w = gray.shape[:2]
    total_area = float(h * w)

    masks = _shape_component_masks(gray)
    contours: List[np.ndarray] = []
    for mask in masks:
        cleaned = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, np.ones((3, 3), dtype=np.uint8), iterations=1)
        found, _ = cv2.findContours(cleaned, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        for contour in found:
            area = float(cv2.contourArea(contour))
            if area >= total_area * 0.95:
                continue
            contours.append(contour)

    if not contours:
        return []

    tpl = get_shape_template(shape)
    verifier = _get_shape_verifier(shape)
    rows: List[Tuple[float, Match]] = []

    for contour in contours:
        area = float(cv2.contourArea(contour))
        if area < float(min_area):
            continue

        if verifier is not None and not verifier(contour):
            continue

        score = float(cv2.matchShapes(contour, tpl.contour, cv2.CONTOURS_MATCH_I1, 0.0))
        if score > float(threshold):
            continue

        x, y, w, h = cv2.boundingRect(contour)
        rows.append((score, Match(Rect(int(x), int(y), int(w), int(h)), label=f"{tpl.name}:{score:.3f}")))

    rows.sort(key=lambda item: item[0])

    deduped: List[Tuple[float, Match]] = []
    for score, match in rows:
        if any(_rect_iou(match.rect, keep[1].rect) >= 0.75 for keep in deduped):
            continue
        deduped.append((score, match))

    if max_results > 0:
        deduped = deduped[: int(max_results)]

    return [row[1] for row in deduped]


def find_shapes(
    source: Image,
    shapes: Iterable[Union[str, ShapeTemplate]],
    threshold: float = 0.2,
    min_area: float = 100.0,
    max_results_per_shape: int = 3,
) -> List[Match]:
    out: List[Match] = []
    for shape in shapes:
        out.extend(
            find_shape(
                source,
                shape=shape,
                threshold=threshold,
                min_area=min_area,
                max_results=max_results_per_shape,
            )
        )
    return out
