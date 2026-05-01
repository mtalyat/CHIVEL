"""Tests for CHIVEL. Run with `python test.py`.
"""

from __future__ import annotations

import chivel as cv
import re

def open_application(name: str) -> None:
    cv.key_click(cv.KEY_META)
    cv.wait(1.0)
    cv.type(name)
    cv.wait(0.5)
    cv.key_click(cv.KEY_ENTER)
    cv.wait(1.0)

def close_application() -> None:
    cv.key_click([cv.KEY_ALT, cv.KEY_F4])
    cv.wait(0.5)

def should_test(name: str) -> bool:
    print(f'Prepare for {name} tests. Press "Enter" to continue, "Space" to skip, "Esc" to stop.')
    key = cv.wait_for([cv.KEY_ENTER, cv.KEY_SPACE, cv.KEY_ESCAPE])
    if key == cv.KEY_ESCAPE:
        print("Test aborted.")
        exit(0)
    elif key == cv.KEY_SPACE:
        print("Test skipped.")
        return False
    return True
    

def main() -> None:
    if should_test("key"):
        # open notepad
        open_application("notepad")

        cv.key_click(cv.KEY_A)
        cv.wait(0.5)
        cv.key_click(cv.KEY_BACKSPACE)

        cv.key_down(cv.KEY_B)
        cv.wait(2.0)
        cv.key_up(cv.KEY_B)
        cv.wait(0.5)
        cv.key_click(cv.KEY_BACKSPACE)

        cv.key_click(cv.KEY_C)
        cv.wait(0.5)
        cv.key_click(cv.KEY_BACKSPACE)

        string = "Hello, World!"
        cv.type(string)
        cv.wait(0.5)
        cv.key_click(cv.KEY_BACKSPACE, count=len(string), delay=0.01)

        # close notepad
        close_application()

    if should_test("mouse"):    
        cv.mouse_move((100, 100))
        cv.wait(0.5)

        cv.mouse_click()
        cv.wait(0.5)

        cv.mouse_down()
        cv.wait(0.5)

        cv.mouse_move((200, 200), relative=True)
        cv.wait(0.5)

        cv.mouse_up()
        cv.wait(0.5)

        cv.mouse_click(button=cv.BUTTON_RIGHT)
        cv.wait(0.5)
        cv.mouse_click()
        cv.wait(0.5)

        cv.mouse_scroll(1, 0)
        cv.wait(0.5)
        cv.mouse_scroll(0, 1)
        cv.wait(0.5)

    if should_test("display"):
        for i in range(cv.DISPLAY_COUNT):
            rect = cv.display_get_rect(i)
            print(f"Display {i}: {rect.width}x{rect.height} at ({rect.x}, {rect.y})")
            cv.mouse_move((rect.x + rect.width // 2, rect.y + rect.height // 2))
            cv.wait(0.5)
            display_index = cv.mouse_get_display()
            print(f"Mouse is on display {display_index}")
            cv.wait(0.5)

    if should_test("find"):
        display_index = cv.mouse_get_display()
        cv.set_primary_display(display_index)
        win_image = cv.load("test.png")
        screen = cv.capture()
        matches = cv.find_image(screen, win_image)
        print(f"find_image matches: {len(matches)}")
        if matches:
            for m in matches:
                print(f"Match at {m.rect} with label: {m.label}")
            screen.draw_matches(matches, color=cv.Color(255, 0, 0), thickness=2)
            cv.save(screen, "test_find_image.png")
            print("Saved test_find_image.png")
    
        open_application("notepad")
        term = "File"
        screen = cv.capture()
        matches = cv.find_text(screen, term, threshold=0.0)
        print(f"find_text('{term}') matches: {len(matches)}")
        if matches:
            for m in matches:
                print(f"Match: '{m.label}' at {m.rect}")
            screen.draw_matches(matches, color=cv.Color(0, 255, 0), thickness=2)
            cv.save(screen, "test_find_text.png")
            print("Saved test_find_text.png")
        close_application()

        open_application("notepad")
        term = '''Hello world!'''
        pattern = re.compile('H[^ ]*')
        cv.type(term)
        screen = cv.capture()
        matches = cv.find_text(screen, pattern, threshold=0.0)
        print(f"find_text('{pattern.pattern}') matches: {len(matches)}")
        if matches:
            for m in matches:
                print(f"Match: '{m.label}' at {m.rect}")
            screen.draw_matches(matches, color=cv.Color(0, 255, 255), thickness=2)
            cv.save(screen, "test_find_regex.png")
            print("Saved test_find_regex.png")
        cv.key_click(cv.KEY_BACKSPACE, count=len(term), delay=0.01)
        close_application()

if __name__ == "__main__":
    main()
