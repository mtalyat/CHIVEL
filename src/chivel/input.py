
# Future imports
from __future__ import annotations

# Standard library imports
import ctypes
import json
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional, Sequence, Tuple, Union

# Third-party imports
import cv2
import numpy as np
import pyperclip
from mss import mss
from pynput import keyboard, mouse

# Internal imports
from .capture import DISPLAY_COUNT, capture, display_get_rect
from .constants import (
    BUTTON_LEFT,
    BUTTON_MIDDLE,
    BUTTON_RIGHT,
    KEY_ALT,
    KEY_CTRL,
    KEY_ENTER,
    KEY_ESCAPE,
    KEY_META,
    KEY_SHIFT,
    KEY_SPACE,
    KEY_TAB,
    SIMPLIFY_KEY,
    SIMPLIFY_MOUSE,
    SIMPLIFY_MOVE,
    SIMPLIFY_TIME,
)
from .core import Point, Rect

def get_clipboard() -> str:
    """Get the current text from the clipboard."""
    try:
        return pyperclip.paste()
    except Exception:
        return ''

def set_clipboard(text: str) -> None:
    """Set the clipboard text to the given value."""
    try:
        pyperclip.copy(text)
    except Exception:
        pass


class _EventSlot:
    def __init__(self, hook: "_EventHook", code: int):
        self._hook = hook
        self._code = int(code)

    def __iadd__(self, handler: Callable[..., Any]):
        self._hook.add(self._code, handler)
        return self

    def __isub__(self, handler: Callable[..., Any]):
        self._hook.remove(self._code, handler)
        return self


class _EventHook:
    def __init__(self, name: str):
        self.name = name
        self._handlers: Dict[int, List[Callable[..., Any]]] = {}
        self._global_handlers: List[Callable[..., Any]] = []

    def __iadd__(self, handler: Callable[..., Any]):
        self.add_global(handler)
        return self

    def __isub__(self, handler: Callable[..., Any]):
        self.remove_global(handler)
        return self

    def __getitem__(self, code: int) -> _EventSlot:
        return _EventSlot(self, int(code))

    # Required so syntax like hook[key] += fn works without replacing storage.
    def __setitem__(self, code: int, value: Any) -> None:
        if isinstance(value, _EventSlot):
            return
        raise TypeError(f"{self.name}[code] only supports += and -= with callables")

    def add(self, code: int, handler: Callable[..., Any]) -> None:
        if not callable(handler):
            raise TypeError("handler must be callable")
        key = int(code)
        self._handlers.setdefault(key, []).append(handler)

    def add_global(self, handler: Callable[..., Any]) -> None:
        if not callable(handler):
            raise TypeError("handler must be callable")
        self._global_handlers.append(handler)

    def on(self, code_or_handler: Any, handler: Optional[Callable[..., Any]] = None) -> None:
        if handler is None and callable(code_or_handler):
            self.add_global(code_or_handler)
            return
        if handler is None:
            raise TypeError("on() requires either on(handler) or on(code, handler)")
        self.add(int(code_or_handler), handler)

    def remove(self, code: int, handler: Callable[..., Any]) -> None:
        key = int(code)
        items = self._handlers.get(key)
        if not items:
            return
        try:
            items.remove(handler)
        except ValueError:
            return
        if not items:
            self._handlers.pop(key, None)

    def remove_global(self, handler: Callable[..., Any]) -> None:
        try:
            self._global_handlers.remove(handler)
        except ValueError:
            return

    def off(self, code_or_handler: Any, handler: Optional[Callable[..., Any]] = None) -> None:
        if handler is None and callable(code_or_handler):
            self.remove_global(code_or_handler)
            return
        if handler is None:
            raise TypeError("off() requires either off(handler) or off(code, handler)")
        self.remove(int(code_or_handler), handler)

    def fire(self, code: int, *args: Any, **kwargs: Any) -> None:
        for handler in list(self._global_handlers):
            try:
                handler(*args, **kwargs)
            except Exception:
                pass
        for handler in list(self._handlers.get(int(code), [])):
            try:
                handler(*args, **kwargs)
            except Exception:
                pass


on_key_down = _EventHook("on_key_down")
on_key_up = _EventHook("on_key_up")
on_key_click = _EventHook("on_key_click")
on_mouse_down = _EventHook("on_mouse_down")
on_mouse_up = _EventHook("on_mouse_up")
on_mouse_click = _EventHook("on_mouse_click")
on_mouse_move = _EventHook("on_mouse_move")
on_mouse_scroll = _EventHook("on_mouse_scroll")

_mouse = mouse.Controller()
_keyboard = keyboard.Controller()


@dataclass
class Recording:
    version: int = 1
    recorded_at: float = 0.0
    stop_key: int = KEY_ESCAPE
    events: List[Dict[str, Any]] = field(default_factory=list)
    # Set on load; not serialised. Used to resolve relative step image paths.
    source_path: Optional[str] = field(default=None, compare=False, repr=False)

    def to_dict(self) -> Dict[str, Any]:
        return {
            "version": self.version,
            "recorded_at": self.recorded_at,
            "stop_key": self.stop_key,
            "events": self.events,
        }

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "Recording":
        return cls(
            version=int(data.get("version", 1)),
            recorded_at=float(data.get("recorded_at", 0.0)),
            stop_key=int(data.get("stop_key", KEY_ESCAPE)),
            events=list(data.get("events", [])),
        )

    def save(self, output_dir: str) -> None:
        path = Path(output_dir)
        path.mkdir(parents=True, exist_ok=True)
        json_path = path / "recording.json"
        json_path.write_text(json.dumps(self.to_dict(), indent=2), encoding="utf-8")

    @classmethod
    def load(cls, input_path: str) -> "Recording":
        path = Path(input_path)
        if path.is_dir():
            path = path / "recording.json"
        data = json.loads(path.read_text(encoding="utf-8"))
        recording = cls.from_dict(data)
        recording.source_path = str(path.resolve())
        return recording


def _btn(value: int) -> mouse.Button:
    return {
        BUTTON_LEFT: mouse.Button.left,
        BUTTON_RIGHT: mouse.Button.right,
        BUTTON_MIDDLE: mouse.Button.middle,
    }.get(value, mouse.Button.left)


def _key(value: int):
    special = {
        KEY_SHIFT: keyboard.Key.shift,
        KEY_CTRL: keyboard.Key.ctrl,
        KEY_ALT: keyboard.Key.alt,
        KEY_META: keyboard.Key.cmd,
        KEY_ENTER: keyboard.Key.enter,
        KEY_TAB: keyboard.Key.tab,
        KEY_ESCAPE: keyboard.Key.esc,
        KEY_SPACE: keyboard.Key.space,
    }
    if value in special:
        return special[value]

    if 0x30 <= value <= 0x39 or 0x41 <= value <= 0x5A:
        return chr(value).lower()

    return keyboard.KeyCode.from_vk(value)


def _key_vk(value: Any) -> Optional[int]:
    vk = getattr(value, "vk", None)
    if vk is None and hasattr(value, "value") and hasattr(value.value, "vk"):
        vk = value.value.vk
    return vk


def _to_xy(pos: Any) -> Tuple[int, int]:
    if isinstance(pos, Point):
        return int(pos.x), int(pos.y)
    if isinstance(pos, Rect):
        center = pos.center()
        return int(center.x), int(center.y)
    if isinstance(pos, (tuple, list)) and len(pos) >= 2:
        return int(pos[0]), int(pos[1])
    raise ValueError("pos must be Point, Rect, or tuple/list(x, y)")


def mouse_move(pos: Any, display_index: Optional[int] = None, relative: bool = False) -> None:
    x, y = _to_xy(pos)

    if relative:
        cx, cy = _mouse.position
        _mouse.position = (int(cx) + x, int(cy) + y)
        nx, ny = _mouse.position
        di = _display_index_for_point(int(nx), int(ny))
        point = Point(int(nx), int(ny))
        on_mouse_move.fire(-1, point, di)
        on_mouse_move.fire(di, point, di)
        return

    rect = display_get_rect(display_index)
    _mouse.position = (rect.x + x, rect.y + y)
    nx, ny = _mouse.position
    di = _display_index_for_point(int(nx), int(ny))
    point = Point(int(nx), int(ny))
    on_mouse_move.fire(-1, point, di)
    on_mouse_move.fire(di, point, di)
    return


def mouse_click(button: int = BUTTON_LEFT, count: int = 1, delay: Optional[float] = None) -> None:
    """
    Click a mouse button one or more times, with a delay between clicks.
    """
    if delay is None:
        delay = 0.0 if count <= 1 else 0.1
    btn = _btn(button)
    for _ in range(max(0, count)):
        _mouse.press(btn)
        on_mouse_down.fire(button, button)
        if delay > 0:
            time.sleep(delay)
        _mouse.release(btn)
        on_mouse_up.fire(button, button)
        on_mouse_click.fire(button, button)


def mouse_down(button: int = BUTTON_LEFT) -> None:
    _mouse.press(_btn(button))
    on_mouse_down.fire(button, button)


def mouse_up(button: int = BUTTON_LEFT) -> None:
    _mouse.release(_btn(button))
    on_mouse_up.fire(button, button)


def mouse_scroll(vertical: int, horizontal: int = 0) -> None:
    _mouse.scroll(horizontal, vertical)
    on_mouse_scroll.fire(vertical, vertical=vertical, horizontal=horizontal)


def mouse_get_location() -> Tuple[Point, int]:
    x, y = _mouse.position
    display_index = _display_index_for_point(int(x), int(y))
    return Point(int(x), int(y)), display_index


def mouse_get_display() -> int:
    x, y = _mouse.position
    return _display_index_for_point(int(x), int(y))


def _display_index_for_point(x: int, y: int) -> int:
    with mss() as sct:
        monitors = sct.monitors[1:]
    for idx, mon in enumerate(monitors):
        left = int(mon["left"])
        top = int(mon["top"])
        right = left + int(mon["width"])
        bottom = top + int(mon["height"])
        if left <= x < right and top <= y < bottom:
            return idx
    return -1


def type(text: str, delay: float = 0.01) -> None:
    """
    Types text character by character, with a delay between each character.
    """
    for ch in text:
        _keyboard.type(ch)
        if delay > 0:
            time.sleep(delay)


def pause(prompt: str = "Press Enter to continue...") -> None:
    input(prompt)


def _normalize_keys(keys: Union[int, Sequence[int]]) -> List[int]:
    if isinstance(keys, int):
        return [int(keys)]
    out = [int(k) for k in keys]
    if not out:
        raise ValueError("keys must contain at least one key code")
    return out


def key_click(keys: Union[int, Sequence[int]], count: int = 1, delay: Optional[float] = None) -> None:
    """
    Press and release one or more keys, optionally multiple times, with a delay between presses.
    """
    normalized = _normalize_keys(keys)
    if delay is None:
        delay = 0.0 if len(normalized) <= 1 else 0.1
    for _ in range(max(0, count)):
        for key in normalized:
            _keyboard.press(_key(key))
            on_key_down.fire(key, key)
        if delay > 0:
            time.sleep(delay)
        for key in normalized:
            _keyboard.release(_key(key))
            on_key_up.fire(key, key)
            on_key_click.fire(key, key)


def key_down(keys: Union[int, Sequence[int]]) -> None:
    for key in _normalize_keys(keys):
        _keyboard.press(_key(key))
        on_key_down.fire(key, key)


def key_up(keys: Union[int, Sequence[int]]) -> None:
    for key in _normalize_keys(keys):
        _keyboard.release(_key(key))
        on_key_up.fire(key, key)


def wait_for(keys_or_buttons: Union[int, Sequence[int]], delay: float = 0.01, timeout: float = -1) -> Optional[int]:
    """
    Wait for one of the requested keys or mouse buttons to be pressed and return its code.
    Supports both keyboard keys and mouse buttons. Polls at the given delay interval.
    If timeout > 0, returns None if no input is detected within timeout seconds.
    """
    if isinstance(keys_or_buttons, int):
        wanted = {int(keys_or_buttons)}
    else:
        wanted = {int(k) for k in keys_or_buttons}

    if not wanted:
        raise ValueError("keys_or_buttons must contain at least one key/button code")

    pressed: dict[str, Optional[int]] = {"code": None}

    def on_press_key(key: Any) -> Any:
        vk = _key_vk(key)
        if vk in wanted:
            pressed["code"] = int(vk)
            on_key_down.fire(int(vk), int(vk))
        return None

    def on_click(x, y, button, pressed_state):
        if not pressed_state:
            return None
        btn_map = {
            BUTTON_LEFT: mouse.Button.left,
            BUTTON_RIGHT: mouse.Button.right,
            BUTTON_MIDDLE: mouse.Button.middle,
        }
        for code, btn in btn_map.items():
            if btn == button and code in wanted:
                pressed["code"] = code
                on_mouse_down.fire(code, code)
        return None

    key_listener = keyboard.Listener(on_press=on_press_key)
    mouse_listener = mouse.Listener(on_click=on_click)
    key_listener.start()
    mouse_listener.start()
    start_time = time.time()
    while pressed["code"] is None:
        if timeout > 0 and (time.time() - start_time) >= timeout:
            key_listener.stop()
            mouse_listener.stop()
            return None
        time.sleep(delay)
    key_listener.stop()
    mouse_listener.stop()
    return pressed["code"]


def check_for(keys_or_buttons: Union[int, Sequence[int]]) -> Optional[int]:
    """
    Checks if any key or mouse button in the sequence is currently pressed.
    Returns the first code found, or None if none are pressed.
    """
    if isinstance(keys_or_buttons, int):
        wanted = [int(keys_or_buttons)]
    else:
        wanted = [int(k) for k in keys_or_buttons]
    # Check keyboard
    try:
        user32 = ctypes.windll.user32
        for code in wanted:
            # Keyboard: 0x01..0xFE
            if 0x01 <= code <= 0xFE:
                state = user32.GetAsyncKeyState(code)
                if state & 0x8000:
                    return code
        # Mouse buttons
        btn_vk = {
            BUTTON_LEFT: 0x01,
            BUTTON_RIGHT: 0x02,
            BUTTON_MIDDLE: 0x04,
        }
        for code in wanted:
            vk = btn_vk.get(code)
            if vk is not None:
                state = user32.GetAsyncKeyState(vk)
                if state & 0x8000:
                    return code
    except Exception:
        pass
    return None


def _simplify_events(
    events: List[Dict[str, Any]],
    simplify: int,
    simplify_threshold: Optional[float] = None,
) -> List[Dict[str, Any]]:
    out = events

    if simplify & SIMPLIFY_MOVE:
        compact: List[Dict[str, Any]] = []
        for evt in out:
            if compact and evt["type"] == "mouse_move" and compact[-1]["type"] == "mouse_move":
                compact[-1] = evt
            else:
                compact.append(evt)
        out = compact

    if simplify & SIMPLIFY_MOUSE:
        out = [e for e in out if not (e["type"] in {"mouse_down", "mouse_up"} and e.get("button") is None)]

    if simplify & SIMPLIFY_KEY:
        out = [e for e in out if e["type"] != "key_unknown"]

    if simplify_threshold is not None and simplify_threshold > 0 and len(out) > 1:
        # Rebuild timestamps so no gap exceeds simplify_threshold.
        adjusted: List[Dict[str, Any]] = [dict(out[0])]
        for evt in out[1:]:
            prev_time = adjusted[-1]["time"]
            gap = evt["time"] - prev_time
            capped = min(gap, simplify_threshold)
            new_evt = dict(evt)
            new_evt["time"] = adjusted[-1]["time"] + capped
            adjusted.append(new_evt)
        out = adjusted

    if simplify & SIMPLIFY_TIME and out:
        first = out[0]["time"]
        for evt in out:
            evt["dt"] = round(evt["time"] - first, 6)

    return out


def record(
    output_dir: Optional[str] = None,
    simplify: int = 0,
    simplify_threshold: Optional[float] = None,
    stop_key: int = KEY_ESCAPE,
    step_key: Optional[int] = None,
    step_size: Tuple[int, int] = (50, 50),
) -> Recording:
    simplify_flags = int(simplify)
    events: List[Dict[str, Any]] = []
    start = time.time()
    stop = {"value": False}
    step_index = {"value": 0}

    # Resolve the recording directory for step screenshots.
    _step_dir: Optional[Path] = None
    if step_key is not None:
        if output_dir is None:
            raise ValueError("output_dir must be provided when step_key is set")
        _step_dir = Path(output_dir)
        _step_dir.mkdir(parents=True, exist_ok=True)

    def now() -> float:
        return time.time() - start

    def on_move(x: int, y: int) -> None:
        # In step mode mouse movement is not recorded; only steps are.
        if step_key is None:
            events.append({"type": "mouse_move", "x": x, "y": y, "time": now()})

    def on_click(x: int, y: int, button: mouse.Button, pressed: bool) -> None:
        events.append(
            {
                "type": "mouse_down" if pressed else "mouse_up",
                "x": x,
                "y": y,
                "button": str(button),
                "time": now(),
            }
        )

    def on_scroll(x: int, y: int, dx: int, dy: int) -> None:
        events.append({"type": "mouse_scroll", "x": x, "y": y, "dx": dx, "dy": dy, "time": now()})

    def _capture_step() -> None:
        mx, my = _mouse.position
        sw, sh = step_size
        region = {
            "left": int(mx) - sw // 2,
            "top": int(my) - sh // 2,
            "width": sw,
            "height": sh,
        }
        filename = f"step_{step_index['value']:04d}.png"
        full_path = _step_dir / filename  # type: ignore[operator]
        with mss() as sct:
            shot = sct.grab(region)
        cv2.imwrite(str(full_path), np.array(shot))
        # Image lives in the same directory as recording.json — store filename only.
        image_ref = filename
        events.append({"type": "step", "image": image_ref, "x": int(mx), "y": int(my), "time": now()})
        step_index["value"] += 1

    def on_press(key: Any) -> Any:
        vk = getattr(key, "vk", None)
        if vk is None and hasattr(key, "value") and hasattr(key.value, "vk"):
            vk = key.value.vk
        if step_key is not None and vk == step_key:
            _capture_step()
            return None
        events.append({"type": "key_down", "key": str(key), "vk": vk, "time": now()})
        if vk == stop_key:
            stop["value"] = True
            return False
        return None

    def on_release(key: Any) -> Any:
        vk = getattr(key, "vk", None)
        if vk is None and hasattr(key, "value") and hasattr(key.value, "vk"):
            vk = key.value.vk
        if step_key is not None and vk == step_key:
            return None
        events.append({"type": "key_up", "key": str(key), "vk": vk, "time": now()})
        return False if stop["value"] else None

    mouse_listener = mouse.Listener(on_move=on_move, on_click=on_click, on_scroll=on_scroll)
    key_listener = keyboard.Listener(on_press=on_press, on_release=on_release)

    mouse_listener.start()
    key_listener.start()
    key_listener.join()
    mouse_listener.stop()

    recording = Recording(
        version=1,
        recorded_at=time.time(),
        stop_key=stop_key,
        events=_simplify_events(events, simplify_flags, simplify_threshold),
    )

    if output_dir is not None:
        recording.save(output_dir)

    return recording


def _btn_from_str(value: str) -> mouse.Button:
    mapping = {
        "Button.left": mouse.Button.left,
        "Button.right": mouse.Button.right,
        "Button.middle": mouse.Button.middle,
    }
    return mapping.get(value, mouse.Button.left)


def _to_gray(arr: np.ndarray) -> np.ndarray:
    if len(arr.shape) == 2:
        return arr
    if arr.shape[2] == 4:
        return cv2.cvtColor(arr, cv2.COLOR_BGRA2GRAY)
    return cv2.cvtColor(arr, cv2.COLOR_BGR2GRAY)


def _find_step_center_on_any_display(template_arr: np.ndarray, threshold: float = 0.8) -> Optional[Tuple[int, int]]:
    template_gray = _to_gray(template_arr)
    th, tw = template_gray.shape[:2]
    best_score = -1.0
    best_center: Optional[Tuple[int, int]] = None

    for display_index in range(DISPLAY_COUNT):
        shot = capture(display_index=display_index).array
        source_gray = _to_gray(shot)
        sh, sw = source_gray.shape[:2]
        if sh < th or sw < tw:
            continue

        result = cv2.matchTemplate(source_gray, template_gray, cv2.TM_CCOEFF_NORMED)
        _, max_val, _, max_loc = cv2.minMaxLoc(result)
        if max_val > best_score:
            rect = display_get_rect(display_index)
            cx = rect.x + int(max_loc[0]) + tw // 2
            cy = rect.y + int(max_loc[1]) + th // 2
            best_center = (cx, cy)
            best_score = float(max_val)

    if best_center is not None and best_score >= threshold:
        return best_center
    return None


def play(recording: Any, speed: float = 1.0) -> None:
    source_path: Optional[str] = None
    if isinstance(recording, Recording):
        events = recording.events
        source_path = recording.source_path
    elif isinstance(recording, (str, Path)):
        loaded = Recording.load(str(recording))
        events = loaded.events
        source_path = loaded.source_path
    elif isinstance(recording, dict):
        events = Recording.from_dict(recording).events
    else:
        raise ValueError("recording must be a Recording, file path, or dict")

    if not events:
        return

    has_step_events = any(evt.get("type") == "step" for evt in events)
    step_ready = not has_step_events

    base_time = events[0]["time"]
    play_start = time.time()

    for evt in events:
        target = play_start + (evt["time"] - base_time) / max(speed, 1e-6)
        delay = target - time.time()
        if delay > 0:
            time.sleep(delay)

        kind = evt["type"]
        if kind == "mouse_move":
            if not has_step_events:
                _mouse.position = (evt["x"], evt["y"])
        elif kind == "mouse_down":
            if has_step_events and not step_ready:
                continue
            if not has_step_events:
                _mouse.position = (evt["x"], evt["y"])
            _mouse.press(_btn_from_str(evt.get("button", "Button.left")))
        elif kind == "mouse_up":
            if has_step_events and not step_ready:
                continue
            if not has_step_events:
                _mouse.position = (evt["x"], evt["y"])
            _mouse.release(_btn_from_str(evt.get("button", "Button.left")))
        elif kind == "mouse_scroll":
            if has_step_events and not step_ready:
                continue
            if not has_step_events:
                _mouse.position = (evt["x"], evt["y"])
            _mouse.scroll(evt.get("dx", 0), evt.get("dy", 0))
        elif kind == "key_down":
            vk = evt.get("vk")
            if vk is not None:
                _keyboard.press(keyboard.KeyCode.from_vk(vk))
        elif kind == "key_up":
            vk = evt.get("vk")
            if vk is not None:
                _keyboard.release(keyboard.KeyCode.from_vk(vk))
        elif kind == "step":
            image_ref = evt.get("image", "")
            if image_ref:
                img_path = Path(image_ref)
                if not img_path.is_absolute() and source_path is not None:
                    img_path = Path(source_path).parent / img_path
                template_arr = cv2.imread(str(img_path))
                if template_arr is not None:
                    center = _find_step_center_on_any_display(template_arr, threshold=0.8)
                    if center is not None:
                        _mouse.position = center
                        step_ready = True
                    else:
                        step_ready = False
