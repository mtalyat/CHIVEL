from __future__ import annotations

from chivel.core import Point, Rect
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
