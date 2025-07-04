from typing import Any, List, Tuple, Optional

class Point:
    x: int
    y: int
    def __init__(self, x: int, y: int) -> None: ...
    def __repr__(self) -> str: ...

class Rect:
    x: int
    y: int
    width: int
    height: int
    def __init__(self, x: int, y: int, width: int, height: int) -> None: ...
    def __repr__(self) -> str: ...
    def get_position(self) -> Point: ...
    def get_size(self) -> Point: ...

class Match:
    rect: Rect
    label: str | None
    def __init__(self, rect: Rect, label: str | None = None) -> None: ...
    def __repr__(self) -> str: ...

class Color:
    r: int
    g: int
    b: int
    a: int
    def __init__(self, r: int, g: int, b: int, a: int = 255) -> None: ...
    def __repr__(self) -> str: ...

class Image:
    def __init__(self, width: int, height: int, channels: int = 3) -> None: ...
    def get_size(self) -> Point: ...
    def show(self, window_name: str = ...) -> None: ...
    def clone(self) -> 'Image': ...
    def crop(self, rect: Rect) -> None: ...
    def grayscale(self) -> None: ...
    def scale(self, x: float, y: float = ...) -> None: ...
    def rotate(self, angle: float) -> None: ...
    def flip(self, flip: int) -> None: ...
    def resize(self, size: Point) -> None: ...
    def draw_rect(self, rect: Rect, color: Optional[Color] = ..., thickness: int = 2) -> None: ...
    def draw_matches(self, matches: List[Match], color: Optional[Color] = ..., thickness: int = 2) -> None: ...
    def draw_line(self, start: Point, end: Point, color: Optional[Color] = ..., thickness: int = 2) -> None: ...
    def draw_text(self, text: str, pos: Point, color: Optional[Color] = ..., font_size: int = ..., thickness: int = ...) -> None: ...
    def draw_ellipse(self, center: Point, radius: int | Tuple[int, int], color: Optional[Color] = ..., thickness: int = 2, angle: float = 0) -> None: ...
    def draw_image(self, image: 'Image', pos: Point, alpha: float = 1.0) -> None: ...
    def invert(self) -> None: ...
    def brightness(self, value: float) -> None: ...
    def contrast(self, value: float = 1.0) -> None: ...
    def sharpen(self, value: float = 1.0) -> None: ...
    def blur(self, value: int = 3) -> None: ...
    def threshold(self, threshold: float = 128.0, maxValue: float = 255.0) -> None: ...
    def normalize(self, alpha: float = 0.0, beta: float = 255.0) -> None: ...
    def edge(self, threshold1: float = 100.0, threshold2: float = 200.0) -> None: ...
    def emboss(self) -> None: ...
    def split(self) -> List['Image']: ...
    def merge(self, channels: List['Image']) -> None: ...
    def convert(self, color_space: int) -> None: ...
    def range(self, lower: Color, upper: Color) -> None: ...
    def mask(self, mask: 'Image') -> None: ...

def load(path: str) -> Image: ...
def save(image: Image, path: str) -> None: ...
def capture(display_index: int = ..., rect: Rect = ...) -> Image: ...
def find_image(source: Image, search: Image, threshold: float = 0.8) -> List[Match]: ...
def find_text(source: Image, search: str, threshold: float = 0.0, text_level: int = ...) -> List[Match]: ...
def wait(seconds: float) -> None: ...
def mouse_move(pos: Any, display_index: int = ...) -> None: ...
def mouse_click(button: int = ..., count: int = ...) -> None: ...
def mouse_down(button: int = ...) -> None: ...
def mouse_up(button: int = ...) -> None: ...
def mouse_scroll(vertical: int, horizontal: int = ...) -> None: ...
def mouse_get_location() -> Tuple[Point, int]: ...
def mouse_get_display() -> int: ...
def type(text: str, wait: float = ...) -> None: ...
def key_click(key: int, count: int = ...) -> None: ...
def key_down(key: int) -> None: ...
def key_up(key: int) -> None: ...
def record(output_path: str, simplify: bool = ..., stop_key: int = ...) -> None: ...
def display_get_rect(display_index: int = ...) -> Rect: ...

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

FLIP_VERTICAL: int
FLIP_HORIZONTAL: int
FLIP_BOTH: int

COLOR_SPACE_UNKNOWN: int
COLOR_SPACE_BGR: int
COLOR_SPACE_BGRA: int
COLOR_SPACE_RGB: int
COLOR_SPACE_RGBA: int
COLOR_SPACE_GRAY: int
COLOR_SPACE_HSV: int