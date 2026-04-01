from __future__ import annotations

from pathlib import Path
from typing import List, Union

import cv2

from .core import Image


def load(path: str) -> Union[str, Image, List[Union[str, Image]]]:
    p = Path(path)
    if p.is_dir():
        out: List[Union[str, Image]] = []
        for child in sorted(p.iterdir()):
            if child.is_file():
                out.append(load(str(child)))
        return out

    suffix = p.suffix.lower()
    if suffix in {".txt", ".md", ".log", ".json", ".csv"}:
        return p.read_text(encoding="utf-8")

    arr = cv2.imread(str(p), cv2.IMREAD_UNCHANGED)
    if arr is None:
        raise ValueError(f"Unsupported or unreadable file: {path}")
    return Image.from_array(arr)


def save(image: Image, path: str) -> None:
    ok = cv2.imwrite(path, image.array)
    if not ok:
        raise ValueError(f"Failed to save image to: {path}")


def show(image: Image, name: str = "image", blocking: bool = True) -> None:
    cv2.imshow(name, image.array)
    cv2.waitKey(0 if blocking else 1)
