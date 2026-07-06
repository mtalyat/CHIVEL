"""Quick sandbox for the CHIVEL library.

Run examples:
  python test.py image
  python test.py capture
  python test.py match
  python test.py ocr
  python test.py input --allow-input
  python test.py record
  python test.py play --allow-input
    python test.py mouse-display
  python test.py step-record
  python test.py step-play --allow-input
"""

from __future__ import annotations

import argparse
import time
from pathlib import Path

import chivel as cv
from pynput import keyboard


def demo_image() -> None:
    img = cv.Image(640, 360)
    img.draw_text("CHIVEL", cv.Point(20, 60), color=cv.Color(0, 255, 255), font_size=2, thickness=3)
    img.draw_rect(cv.Rect(15, 90, 220, 120), color=cv.Color(255, 0, 0), thickness=3)
    img.draw_line(cv.Point(15, 90), cv.Point(235, 210), color=cv.Color(0, 255, 0), thickness=2)
    img.blur(3)

    out = Path("out_image.png")
    cv.save(img, str(out))
    print(f"Saved {out.resolve()}")


def demo_capture() -> None:
    shot = cv.capture(display_index=0)
    size = shot.get_size()
    print(f"Captured display 0: {size.x}x{size.y}")

    out = Path("out_capture.png")
    cv.save(shot, str(out))
    print(f"Saved {out.resolve()}")


def demo_match() -> None:
    source = cv.capture(display_index=0)
    size = source.get_size()

    # Take a small region from the current screen and search for it in that same screen.
    probe_rect = cv.Rect(max(0, size.x // 4), max(0, size.y // 4), min(150, size.x), min(80, size.y))
    probe = source.clone()
    probe.crop(probe_rect)

    matches = cv.find_image(source, probe, threshold=0.95)
    print(f"find_image matches: {len(matches)}")

    if matches:
        source.draw_matches(matches[:5], color=cv.Color(255, 0, 255), thickness=2)
        out = Path("out_match.png")
        cv.save(source, str(out))
        print(f"Saved {out.resolve()}")


def demo_ocr() -> None:
    source = cv.capture(display_index=0)
    term = "File"
    try:
        matches = cv.find_text(source, term, threshold=0.0, text_level=cv.TEXT_WORD)
    except RuntimeError as err:
        print("OCR not ready:")
        print(err)
        return

    print(f"find_text('{term}') matches: {len(matches)}")
    if matches:
        source.draw_matches(matches[:20], color=cv.Color(0, 255, 255), thickness=2)
        out = Path("out_ocr.png")
        cv.save(source, str(out))
        print(f"Saved {out.resolve()}")


def demo_record() -> None:
    out = Path("out_record.py")
    print("Recording mouse and keyboard events.")
    print("Press ESC to stop and save.")
    recording = cv.record(
        output_dir=str(out),
        # simplify=cv.SIMPLIFY_MOVE | cv.SIMPLIFY_MOUSE | cv.SIMPLIFY_KEY | cv.SIMPLIFY_TIME
        simplify=0,
    )

    events = recording.events
    print(f"Recorded {len(events)} events -> {out.resolve()}")

    counts: dict = {}
    for e in events:
        counts[e["type"]] = counts.get(e["type"], 0) + 1
    for kind, n in sorted(counts.items()):
        print(f"  {kind}: {n}")


def demo_play(allow_input: bool) -> None:
    recording_path = Path("out_record.py")
    if not recording_path.exists():
        print(f"No recording found at {recording_path.resolve()}")
        print("Run 'python test.py record' first.")
        return

    if not allow_input:
        print("Refusing to replay input events. Re-run with --allow-input to enable.")
        return

    print(f"Replaying from {recording_path.resolve()}")
    print("Move focus to target window now. Starting in 3 seconds...")
    cv.wait(3.0)
    cv.play(str(recording_path))
    print("Playback complete.")


def demo_input(allow_input: bool) -> None:
    if not allow_input:
        print("Refusing to send input events. Re-run with --allow-input to enable.")
        return

    print("Moving mouse and typing in 2 seconds. Focus a safe text field now.")
    cv.wait(2.0)
    point, display = cv.mouse_get_location()
    print(f"Mouse at ({point.x}, {point.y}) on display {display}")

    cv.mouse_move((point.x + 20, point.y + 20))
    cv.mouse_click(cv.BUTTON_LEFT, count=1)
    cv.type("hello from chivel", wait=0.03)
    print("Input demo finished.")


def demo_step_record() -> None:
    out = Path("out_step_record.py")
    # F8 (VK 0x77) is the step key — press it to snapshot where the mouse is.
    # Mouse movement is suppressed between steps; only clicks/scrolls carry through.
    step_key = 0x77
    print("Step recording mode.")
    print("  Move mouse over a UI element, then press F8 to mark it as a step.")
    print("  Press ESC to stop and save.")
    recording = cv.record(
        output_dir=str(out),
        step_key=step_key,
        simplify=cv.SIMPLIFY_ALL,
        simplify_threshold=0.5,
    )

    steps = [e for e in recording.events if e["type"] == "step"]
    others = [e for e in recording.events if e["type"] != "step"]
    print(f"Saved {len(steps)} step(s) and {len(others)} other event(s) -> {out.resolve()}")
    for i, s in enumerate(steps):
        print(f"  step {i}: {s['image']}  @ ({s['x']}, {s['y']})");


def demo_step_play(allow_input: bool) -> None:
    recording_path = Path("out_step_record.py")
    if not recording_path.exists():
        print(f"No step recording found at {recording_path.resolve()}")
        print("Run 'python test.py step-record' first.")
        return

    if not allow_input:
        print("Refusing to replay input events. Re-run with --allow-input to enable.")
        return

    print(f"Replaying step recording from {recording_path.resolve()}")
    print("Starting in 3 seconds...")
    cv.wait(3.0)
    cv.play(str(recording_path))
    print("Step playback complete.")


def demo_mouse_display() -> None:
    stop = {"value": False}

    def on_press(key: object) -> bool | None:
        if key == keyboard.Key.esc:
            stop["value"] = True
            return False
        return None

    print("Tracking mouse display. Press ESC to stop.")
    listener = keyboard.Listener(on_press=on_press)
    listener.start()
    try:
        while not stop["value"]:
            point, display = cv.mouse_get_location()
            print(f"\rDisplay: {display}  X: {point.x}  Y: {point.y}    ", end="", flush=True)
            time.sleep(0.05)
    finally:
        listener.stop()
    print()


def main() -> None:
    parser = argparse.ArgumentParser(description="CHIVEL sandbox runner")
    parser.add_argument(
        "demo",
        choices=["image", "capture", "match", "ocr", "input", "record", "play", "mouse-display", "step-record", "step-play"],
        help="Which demo to run",
    )
    parser.add_argument(
        "--allow-input",
        action="store_true",
        help="Allow real mouse/keyboard input events for the input demo",
    )

    args = parser.parse_args()

    if args.demo == "image":
        demo_image()
    elif args.demo == "capture":
        demo_capture()
    elif args.demo == "match":
        demo_match()
    elif args.demo == "ocr":
        demo_ocr()
    elif args.demo == "input":
        demo_input(args.allow_input)
    elif args.demo == "record":
        demo_record()
    elif args.demo == "play":
        demo_play(args.allow_input)
    elif args.demo == "mouse-display":
        demo_mouse_display()
    elif args.demo == "step-record":
        demo_step_record()
    elif args.demo == "step-play":
        demo_step_play(args.allow_input)


if __name__ == "__main__":
    main()
