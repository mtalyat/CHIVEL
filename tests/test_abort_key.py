import chivel as cv


def test_default_abort_key_is_escape():
    cv.set_abort_key()
    assert cv.get_abort_key() == [cv.KEY_ESCAPE]
    cv.clear_abort_key()


def test_set_abort_key_exits_when_combo_is_pressed(monkeypatch):
    cv.set_abort_key([cv.KEY_CTRL, cv.KEY_ESCAPE])

    def fake_check_for(keys):
        if isinstance(keys, int):
            return keys
        return keys[0] if keys else None

    monkeypatch.setattr("chivel.input.check_for", fake_check_for)

    try:
        cv.wait(0.01)
    except SystemExit:
        pass
    else:
        raise AssertionError("Abort key combo should trigger SystemExit during wait().")
    finally:
        cv.clear_abort_key()


def test_set_abort_key_none_disables_abort_behavior(monkeypatch):
    cv.set_abort_key(None)
    assert cv.get_abort_key() is None

    def fake_check_for(keys):
        return keys if isinstance(keys, int) else keys[0]

    monkeypatch.setattr("chivel.input.check_for", fake_check_for)

    try:
        cv.wait(0.01)
    except SystemExit:
        raise AssertionError("Setting abort key to None should disable abort behavior.")
    finally:
        cv.clear_abort_key()
