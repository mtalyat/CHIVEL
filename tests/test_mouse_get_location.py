from __future__ import annotations

import sys
from pathlib import Path

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
    calls = []

    def fake_key_click(keys, count=1, delay=None):
        calls.append((keys, count, delay))

    monkeypatch.setattr(input_mod, "key_click", fake_key_click)

    input_mod.copy()
    input_mod.cut()
    input_mod.paste()
    input_mod.select_all()
    input_mod.undo()
    input_mod.redo()

    assert calls == [
        ([input_mod.KEY_CTRL, input_mod.KEY_C], 1, None),
        ([input_mod.KEY_CTRL, input_mod.KEY_X], 1, None),
        ([input_mod.KEY_CTRL, input_mod.KEY_V], 1, None),
        ([input_mod.KEY_CTRL, input_mod.KEY_A], 1, None),
        ([input_mod.KEY_CTRL, input_mod.KEY_Z], 1, None),
        ([input_mod.KEY_CTRL, input_mod.KEY_Y], 1, None),
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
