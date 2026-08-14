
# Future imports
from __future__ import annotations

# Standard library imports
import ctypes
import runpy
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
from .capture import display_get_rect
from .constants import (
    BUTTON_LEFT,
    BUTTON_MIDDLE,
    BUTTON_RIGHT,
    KEY_A,
    KEY_ALT,
    KEY_C,
    KEY_CTRL,
    KEY_ENTER,
    KEY_ESCAPE,
    KEY_META,
    KEY_SHIFT,
    KEY_SPACE,
    KEY_TAB,
    KEY_V,
    KEY_X,
    KEY_Y,
    KEY_Z,
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
_ABORT_KEY: Optional[List[int]] = [KEY_ESCAPE]


def get_abort_key() -> Optional[List[int]]:
    """Get the current global abort combo, or None when disabled."""
    return None if _ABORT_KEY is None else list(_ABORT_KEY)


def set_abort_key(keys: Optional[Union[int, Sequence[int]]] = KEY_ESCAPE) -> None:
    """Set a global abort combo. When all keys in the combo are held, SystemExit is raised."""
    global _ABORT_KEY
    if keys is None:
        _ABORT_KEY = None
        return
    _ABORT_KEY = _normalize_keys(keys)


def clear_abort_key() -> None:
    """Disable the global abort combo."""
    global _ABORT_KEY
    _ABORT_KEY = None


def _check_abort_key() -> None:
    if _ABORT_KEY is None:
        return
    if all(check_for(key) is not None for key in _ABORT_KEY):
        raise SystemExit("Abort key pressed")


@dataclass
class Recording:
    version: int = 1
    recorded_at: float = 0.0
    stop_key: int = KEY_ESCAPE
    events: List[Dict[str, Any]] = field(default_factory=list)

    @staticmethod
    def _button_expr(value: Any) -> str:
        mapping = {
            "Button.left": "cv.BUTTON_LEFT",
            "Button.right": "cv.BUTTON_RIGHT",
            "Button.middle": "cv.BUTTON_MIDDLE",
        }
        return mapping.get(str(value), "cv.BUTTON_LEFT")

    @staticmethod
    def _event_to_python(event: Dict[str, Any], base_time: float) -> List[str]:
        lines: List[str] = []
        delta = max(0.0, float(event.get("time", base_time)) - float(base_time))
        lines.append(f"    cv.wait({delta:.6f} / speed)")

        kind = event.get("type")
        if kind == "mouse_move":
            lines.append(f"    cv.mouse_move(({int(event.get('x', 0))}, {int(event.get('y', 0))}))")
        elif kind == "mouse_down":
            lines.append(f"    cv.mouse_down({Recording._button_expr(event.get('button'))})")
        elif kind == "mouse_up":
            lines.append(f"    cv.mouse_up({Recording._button_expr(event.get('button'))})")
        elif kind == "mouse_scroll":
            lines.append(
                f"    cv.mouse_scroll(vertical={int(event.get('dy', 0))}, horizontal={int(event.get('dx', 0))})"
            )
        elif kind == "key_down":
            vk = event.get("vk")
            if vk is not None:
                lines.append(f"    cv.key_down({int(vk)})")
        elif kind == "key_up":
            vk = event.get("vk")
            if vk is not None:
                lines.append(f"    cv.key_up({int(vk)})")
        elif kind == "step":
            image = str(event.get("image", ""))
            lines.append(f"    _move_to_step({image!r})")

        return lines

    def to_python_script(self) -> str:
        lines: List[str] = [
            "from __future__ import annotations",
            "",
            "from pathlib import Path",
            "",
            "import chivel as cv",
            "",
            "",
            "def _move_to_step(image_name: str, threshold: float = 0.8) -> bool:",
            "    template_path = Path(__file__).parent / image_name",
            "    template = cv.load(str(template_path))",
            "    for display_index in range(cv.DISPLAY_COUNT):",
            "        source = cv.capture(display_index=display_index)",
            "        hits = cv.find_image(source, template, threshold=threshold)",
            "        if hits:",
            "            center = hits[0].rect.center()",
            "            cv.mouse_move((center.x, center.y), display_index=display_index)",
            "            return True",
            "    return False",
            "",
            "",
            "def run(speed: float = 1.0) -> None:",
            "    speed = max(float(speed), 1e-6)",
        ]

        if not self.events:
            lines.append("    return")
        else:
            base_time = float(self.events[0].get("time", 0.0))
            prev_time = base_time
            for event in self.events:
                evt_time = float(event.get("time", prev_time))
                event_with_delta = dict(event)
                event_with_delta["time"] = evt_time
                lines.extend(self._event_to_python(event_with_delta, prev_time))
                prev_time = evt_time

        lines.extend(
            [
                "",
                "",
                "if __name__ == \"__main__\":",
                "    run()",
                "",
            ]
        )
        return "\n".join(lines)

    def save(self, output_path: str) -> None:
        script_path = Path(output_path)
        script_path.parent.mkdir(parents=True, exist_ok=True)
        script_path.write_text(self.to_python_script(), encoding="utf-8")


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


def mouse_get_location(global_coords: bool = False) -> Tuple[Point, int]:
    x, y = _mouse.position
    display_index = _display_index_for_point(int(x), int(y))
    if global_coords or display_index < 0:
        return Point(int(x), int(y)), display_index

    rect = display_get_rect(display_index)
    return Point(int(x) - rect.x, int(y) - rect.y), display_index


def mouse_get_location_global() -> Tuple[Point, int]:
    return mouse_get_location(global_coords=True)


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
        _check_abort_key()
        _keyboard.type(ch)
        if delay > 0:
            time.sleep(delay)
            _check_abort_key()


def pause(prompt: str = "Press Enter to continue...") -> None:
    _check_abort_key()
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
        _check_abort_key()
        for key in normalized:
            _keyboard.press(_key(key))
            on_key_down.fire(key, key)
        if delay > 0:
            time.sleep(delay)
            _check_abort_key()
        for key in normalized:
            _keyboard.release(_key(key))
            on_key_up.fire(key, key)
            on_key_click.fire(key, key)


def copy() -> None:
    """Trigger the standard copy shortcut."""
    key_click([KEY_CTRL, KEY_C])


def cut() -> None:
    """Trigger the standard cut shortcut."""
    key_click([KEY_CTRL, KEY_X])


def paste() -> None:
    """Trigger the standard paste shortcut."""
    key_click([KEY_CTRL, KEY_V])


def select_all() -> None:
    """Trigger the standard select-all shortcut."""
    key_click([KEY_CTRL, KEY_A])


def undo() -> None:
    """Trigger the standard undo shortcut."""
    key_click([KEY_CTRL, KEY_Z])


def redo() -> None:
    """Trigger the standard redo shortcut."""
    key_click([KEY_CTRL, KEY_Y])


def key_down(keys: Union[int, Sequence[int]]) -> None:
    for key in _normalize_keys(keys):
        _check_abort_key()
        _keyboard.press(_key(key))
        on_key_down.fire(key, key)


def key_up(keys: Union[int, Sequence[int]]) -> None:
    for key in _normalize_keys(keys):
        _check_abort_key()
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
        _check_abort_key()
        if timeout > 0 and (time.time() - start_time) >= timeout:
            key_listener.stop()
            mouse_listener.stop()
            return None
        time.sleep(delay)
        _check_abort_key()
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
        _step_dir = Path(output_dir).parent
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
        # Image lives next to the generated recording.py script; store filename only.
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


def play(recording: Any, speed: float = 1.0) -> None:
    if not isinstance(recording, (str, Path)):
        raise ValueError("recording must be a Python file path")

    path = Path(recording)
    if not path.is_file():
        raise ValueError("recording must be a Python file path")

    script_globals = runpy.run_path(str(path), run_name="__chivel_recording__")
    run_fn = script_globals.get("run")
    if not callable(run_fn):
        raise ValueError("recording file must define run(speed: float = 1.0)")
    run_fn(max(float(speed), 1e-6))
