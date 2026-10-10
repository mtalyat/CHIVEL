from __future__ import annotations

import sys
from pathlib import Path

import pytest

ROOT_DIR = Path(__file__).resolve().parents[1]
SRC_DIR = ROOT_DIR / "src"
if str(SRC_DIR) not in sys.path:
    sys.path.insert(0, str(SRC_DIR))

from chivel.core import Match, Point, Rect
from chivel import input as input_mod


def test_mouse_get_location_returns_display_local_coordinates(monkeypatch):
    class _Mouse:
        position = (2000, 1500)

    monkeypatch.setattr(input_mod, "_mouse", _Mouse())
    monkeypatch.setattr(input_mod, "_display_index_for_point", lambda x, y: 1)
    monkeypatch.setattr(input_mod, "display_get_rect", lambda display_index=None: Rect(1000, 200, 800, 600))

    pt, display_index = input_mod.mouse_get_location()

    assert display_index == 1
    assert pt == Point(1000, 1300)


def test_mouse_get_location_global_returns_global_coordinates(monkeypatch):
    class _Mouse:
        position = (2000, 1500)

    monkeypatch.setattr(input_mod, "_mouse", _Mouse())
    monkeypatch.setattr(input_mod, "_display_index_for_point", lambda x, y: 1)

    pt, display_index = input_mod.mouse_get_location(global_coords=True)

    assert display_index == 1
    assert pt == Point(2000, 1500)


def test_common_shortcut_helpers(monkeypatch):
    key_calls = []
    clipboard_calls = []

    def fake_key_click(keys, count=1, delay=None):
        key_calls.append((keys, count, delay))

    def fake_set_clipboard(text):
        clipboard_calls.append(("set", text))

    def fake_get_clipboard():
        clipboard_calls.append(("get",))
        return "clipboard-value"

    monkeypatch.setattr(input_mod, "key_click", fake_key_click)
    monkeypatch.setattr(input_mod, "set_clipboard", fake_set_clipboard)
    monkeypatch.setattr(input_mod, "get_clipboard", fake_get_clipboard)

    assert input_mod.copy() == "clipboard-value"
    assert input_mod.cut() == "clipboard-value"
    assert input_mod.paste() == "clipboard-value"
    assert input_mod.copy("copy-text") == "clipboard-value"
    assert input_mod.cut("cut-text") == "clipboard-value"
    assert input_mod.paste("paste-text") == "clipboard-value"
    assert input_mod.paste(None) == "clipboard-value"
    input_mod.select_all()
    input_mod.undo()
    input_mod.redo()

    assert key_calls == [
        ([input_mod.KEY_CTRL, input_mod.KEY_C], 1, None),
        ([input_mod.KEY_CTRL, input_mod.KEY_X], 1, None),
        ([input_mod.KEY_CTRL, input_mod.KEY_V], 1, None),
        ([input_mod.KEY_CTRL, input_mod.KEY_C], 1, None),
        ([input_mod.KEY_CTRL, input_mod.KEY_X], 1, None),
        ([input_mod.KEY_CTRL, input_mod.KEY_V], 1, None),
        ([input_mod.KEY_CTRL, input_mod.KEY_V], 1, None),
        ([input_mod.KEY_CTRL, input_mod.KEY_A], 1, None),
        ([input_mod.KEY_CTRL, input_mod.KEY_Z], 1, None),
        ([input_mod.KEY_CTRL, input_mod.KEY_Y], 1, None),
    ]
    assert clipboard_calls == [
        ("get",),
        ("get",),
        ("get",),
        ("set", "copy-text"),
        ("get",),
        ("set", "cut-text"),
        ("get",),
        ("set", "paste-text"),
        ("get",),
        ("get",),
    ]


def test_mouse_move_accepts_match_and_moves_to_match_center(monkeypatch):
    class _Mouse:
        position = (0, 0)

    monkeypatch.setattr(input_mod, "_mouse", _Mouse())
    monkeypatch.setattr(input_mod, "display_get_rect", lambda display_index=None: Rect(100, 200, 800, 600))
    monkeypatch.setattr(input_mod, "_display_index_for_point", lambda x, y: 0)
    monkeypatch.setattr(input_mod.on_mouse_move, "fire", lambda *args, **kwargs: None)

    input_mod.mouse_move(Match(Rect(10, 20, 40, 20)))

    assert input_mod._mouse.position == (130, 230)


def test_mouse_drag_calls_down_move_up_in_order(monkeypatch):
    calls = []

    monkeypatch.setattr(input_mod, "mouse_down", lambda button=input_mod.BUTTON_LEFT: calls.append(("down", button)))
    monkeypatch.setattr(
        input_mod,
        "mouse_move",
        lambda pos, display_index=None, relative=False: calls.append(("move", pos, display_index, relative)),
    )
    monkeypatch.setattr(input_mod, "mouse_up", lambda button=input_mod.BUTTON_LEFT: calls.append(("up", button)))

    input_mod.mouse_drag((25, 35), button=input_mod.BUTTON_RIGHT, display_index=1, relative=False)

    assert calls == [
        ("down", input_mod.BUTTON_RIGHT),
        ("move", (25, 35), 1, False),
        ("up", input_mod.BUTTON_RIGHT),
    ]


def test_mouse_drag_releases_button_when_move_fails(monkeypatch):
    calls = []

    monkeypatch.setattr(input_mod, "mouse_down", lambda button=input_mod.BUTTON_LEFT: calls.append(("down", button)))

    def _raise_move(*args, **kwargs):
        calls.append(("move",))
        raise RuntimeError("move failed")

    monkeypatch.setattr(input_mod, "mouse_move", _raise_move)
    monkeypatch.setattr(input_mod, "mouse_up", lambda button=input_mod.BUTTON_LEFT: calls.append(("up", button)))

    with pytest.raises(RuntimeError, match="move failed"):
        input_mod.mouse_drag((10, 10), button=input_mod.BUTTON_LEFT)

    assert calls == [("down", input_mod.BUTTON_LEFT), ("move",), ("up", input_mod.BUTTON_LEFT)]
