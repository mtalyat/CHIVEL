from typing import Any, List, Tuple, Optional

class Image:
    def get_size(self) -> Tuple[int, int]: ...

def load(path: str) -> Image: ...
def save(image: Image, path: str) -> None: ...
def show(image: Image, window_name: str = ...) -> None: ...
def capture(rect: Tuple[int, int, int, int] = ..., display_index: int = ...) -> Image: ...
def find(source: Image, search: Any, threshold: float = ..., text_level: int = ...) -> List[Tuple[int, int, int, int]]: ...
def draw(image: Image, rects: Any, color: Optional[Tuple[int, int, int]] = ...) -> None: ...
def wait(seconds: float) -> None: ...
def mouse_move(pos: Any, display_index: int = ...) -> None: ...
def mouse_click(button: int = ..., count: int = ...) -> None: ...
def mouse_down(button: int = ...) -> None: ...
def mouse_up(button: int = ...) -> None: ...
def mouse_scroll(vertical: int, horizontal: int = ...) -> None: ...
def mouse_get_location() -> Tuple[int, int, int]: ...
def mouse_get_display() -> int: ...
def type(text: str, wait: float = ...) -> None: ...
def key_click(key: int, count: int = ...) -> None: ...
def key_down(key: int) -> None: ...
def key_up(key: int) -> None: ...
def record(output_path: str, simplify: bool = ..., stop_key: int = ...) -> None: ...
def display_get_rect(display_index: int = ...) -> Tuple[int, int, int, int]: ...

# Constants
TEXT_BLOCK: int
TEXT_PARAGRAPH: int
TEXT_LINE: int
TEXT_WORD: int
TEXT_SYMBOL: int

DISPLAY_COUNT: int

KEY_BACKSPACE: int
KEY_TAB: int
KEY_ENTER: int
KEY_SHIFT: int
KEY_CTRL: int
KEY_ALT: int
KEY_PAUSE: int
KEY_CAPSLOCK: int
KEY_ESC: int
KEY_SPACE: int
KEY_PAGEUP: int
KEY_PAGEDOWN: int
KEY_END: int
KEY_HOME: int
KEY_LEFT: int
KEY_UP: int
KEY_RIGHT: int
KEY_DOWN: int
KEY_PRINTSCREEN: int
KEY_INSERT: int
KEY_DELETE: int
KEY_NUMLOCK: int
KEY_SCROLLLOCK: int
KEY_NUMPAD0: int
KEY_NUMPAD1: int
KEY_NUMPAD2: int
KEY_NUMPAD3: int
KEY_NUMPAD4: int
KEY_NUMPAD5: int
KEY_NUMPAD6: int
KEY_NUMPAD7: int
KEY_NUMPAD8: int
KEY_NUMPAD9: int
KEY_MULTIPLY: int
KEY_ADD: int
KEY_SEPARATOR: int
KEY_SUBTRACT: int
KEY_DECIMAL: int
KEY_DIVIDE: int
KEY_0: int
KEY_1: int
KEY_2: int
KEY_3: int
KEY_4: int
KEY_5: int
KEY_6: int
KEY_7: int
KEY_8: int
KEY_9: int
KEY_A: int
KEY_B: int
KEY_C: int
KEY_D: int
KEY_E: int
KEY_F: int
KEY_G: int
KEY_H: int
KEY_I: int
KEY_J: int
KEY_K: int
KEY_L: int
KEY_M: int
KEY_N: int
KEY_O: int
KEY_P: int
KEY_Q: int
KEY_R: int
KEY_S: int
KEY_T: int
KEY_U: int
KEY_V: int
KEY_W: int
KEY_X: int
KEY_Y: int
KEY_Z: int
KEY_F1: int
KEY_F2: int
KEY_F3: int
KEY_F4: int
KEY_F5: int
KEY_F6: int
KEY_F7: int
KEY_F8: int
KEY_F9: int
KEY_F10: int
KEY_F11: int
KEY_F12: int

BUTTON_LEFT: int
BUTTON_RIGHT: int
BUTTON_MIDDLE: int
BUTTON_X1: int
BUTTON_X2: int

SIMPLIFY_NONE: int
SIMPLIFY_MOVE: int
SIMPLIFY_MOUSE: int
SIMPLIFY_KEY: int
SIMPLIFY_TIME: int
SIMPLIFY_ALL: int