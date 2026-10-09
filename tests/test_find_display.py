from __future__ import annotations

import sys
from pathlib import Path

ROOT_DIR = Path(__file__).resolve().parents[1]
SRC_DIR = ROOT_DIR / "src"
if str(SRC_DIR) not in sys.path:
    sys.path.insert(0, str(SRC_DIR))

from chivel import find as find_mod
from chivel.core import Match, Rect


def test_find_display_returns_matching_display_in_primary_first_order(monkeypatch):
    monkeypatch.setattr(find_mod, "DISPLAY_COUNT", 3)
    monkeypatch.setattr(find_mod, "get_primary_display", lambda: 1)
    monkeypatch.setattr(find_mod, "_check_abort_key", lambda: None)

    checked_displays = []

    def fake_capture(display_index=None, rect=None):
        checked_displays.append(display_index)
        return {"display": display_index}

    def fake_search_one(source, item, threshold):
        if source["display"] == 2:
            return [Match(Rect(0, 0, 1, 1))]
        return []

    monkeypatch.setattr(find_mod, "capture", fake_capture)
    monkeypatch.setattr(find_mod, "_search_one", fake_search_one)

    found_on = find_mod.find_display("Settings")

    assert found_on == 2
    assert checked_displays == [1, 0, 2]


def test_find_display_returns_negative_one_when_not_found(monkeypatch):
    monkeypatch.setattr(find_mod, "DISPLAY_COUNT", 2)
    monkeypatch.setattr(find_mod, "get_primary_display", lambda: 0)
    monkeypatch.setattr(find_mod, "_check_abort_key", lambda: None)
    monkeypatch.setattr(find_mod, "capture", lambda display_index=None, rect=None: {"display": display_index})
    monkeypatch.setattr(find_mod, "_search_one", lambda source, item, threshold: [])

    found_on = find_mod.find_display("missing")

    assert found_on == -1


def test_expect_display_retries_until_found(monkeypatch):
    monkeypatch.setattr(find_mod, "_check_abort_key", lambda: None)
    monkeypatch.setattr(find_mod.time, "sleep", lambda _seconds: None)

    responses = iter([-1, -1, 1])
    monkeypatch.setattr(
        find_mod,
        "find_display",
        lambda search, threshold=0.8, display_indices=None: next(responses),
    )

    found_on = find_mod.expect_display("ready", interval=0.01, timeout=2.0)

    assert found_on == 1


def test_expect_display_returns_negative_one_on_timeout(monkeypatch):
    monkeypatch.setattr(find_mod, "_check_abort_key", lambda: None)
    monkeypatch.setattr(find_mod.time, "sleep", lambda _seconds: None)
    monkeypatch.setattr(find_mod, "find_display", lambda search, threshold=0.8, display_indices=None: -1)

    times = iter([0.0, 0.2, 0.4, 0.6])
    monkeypatch.setattr(find_mod.time, "time", lambda: next(times))

    found_on = find_mod.expect_display("never", interval=0.01, timeout=0.5)

    assert found_on == -1
