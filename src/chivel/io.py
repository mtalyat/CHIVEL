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
                child_data = load(str(child))
                if isinstance(child_data, list):
                    out.extend(child_data)
                else:
                    out.append(child_data)
        return out

    suffix = p.suffix.lower()
    if suffix in {".txt", ".md", ".log", ".json", ".csv"}:
        return p.read_text(encoding="utf-8")

    arr = cv2.imread(str(p), cv2.IMREAD_UNCHANGED)
    if arr is None:
        raise ValueError(f"Unsupported or unreadable file: {path}")
    return Image.from_array(arr)


def save(data: Image | str, path: str) -> None:
    if isinstance(data, str):
        with open(path, "w", encoding="utf-8") as f:
            f.write(data)
    elif isinstance(data, Image):
        ok = cv2.imwrite(path, data.array)
        if not ok:
            raise ValueError(f"Failed to save image to: {path}")
    else:
        raise ValueError(f"Unsupported data type: {type(data)}")


def show(image: Image, name: str = "image", blocking: bool = True) -> None:
    cv2.imshow(name, image.array)
    cv2.waitKey(0 if blocking else 1)
