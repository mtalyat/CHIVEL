"""Quick sandbox for the CHIVEL library.

Run examples:
  python test.py image
    python test.py overlay
        python test.py text-overlay
        python test.py shapes
  python test.py capture
  python test.py match
  python test.py ocr
    python test.py elements
    python test.py elements --allow-input
  python test.py input --allow-input
  python test.py record
  python test.py play --allow-input
    python test.py mouse-display
  python test.py display-search --display-search-text "Settings"
  python test.py step-record
  python test.py step-play --allow-input
"""

from __future__ import annotations

import argparse
import re
import sys
import time
from pathlib import Path
from typing import List, Optional, Sequence

ROOT_DIR = Path(__file__).resolve().parents[1]
SRC_DIR = ROOT_DIR / "src"
if str(SRC_DIR) not in sys.path:
    sys.path.insert(0, str(SRC_DIR))

import chivel as cv
from pynput import keyboard

def _parse_display_indices(value: str) -> Optional[List[int]]:
    value = (value or "").strip()
    if not value:
        return None

    indices: List[int] = []
    for token in value.split(","):
        part = token.strip()
        if not part:
            continue
        indices.append(int(part))
    return indices or None


def demo_image() -> None:
    img = cv.Image(640, 360)
    img.draw_text("CHIVEL", cv.Point(20, 60), color=cv.Color(0, 255, 255), font_size=2, thickness=3)
    img.draw_rect(cv.Rect(15, 90, 220, 120), color=cv.Color(255, 0, 0), thickness=3)
    img.draw_line(cv.Point(15, 90), cv.Point(235, 210), color=cv.Color(0, 255, 0), thickness=2)
    img.blur(3)

    out = Path("out_image.png")
    cv.save(img, str(out))
    print(f"Saved {out.resolve()}")


def demo_overlay() -> None:
    point, display_index = cv.mouse_get_location_global()
    display_rect = cv.display_get_rect(display_index)
    print("Showing click-through overlay for 10 seconds on current display.")
    print("You can keep clicking and interacting with apps under the overlay.")

    with cv.overlay(display_index=display_index, click_through=True) as ov:
        def update(overlay: cv.ScreenOverlay, frame: int, elapsed: float) -> None:
            pulse = int(20 + (elapsed * 80) % 120)
            local_mouse = overlay.mouse_point_local()

            overlay.clear()
            overlay.draw_text("CHIVEL LIVE OVERLAY", (20, 20), color=cv.Color(0, 255, 255), size=20)
            overlay.draw_text(f"display={display_index} frame={frame}", (20, 46), color=cv.Color(200, 220, 255), size=12)

            box_a = cv.Rect(80, 90, 300, 160)
            box_b = cv.Rect(display_rect.width - 400, display_rect.height - 240, 320, 140)
            overlay.draw_rect(box_a, color=cv.Color(255, 140, 0), thickness=3, label="Target A")
            overlay.draw_rect(box_b, color=cv.Color(0, 220, 120), thickness=3, label="Target B")

            if local_mouse is not None:
                overlay.draw_crosshair((local_mouse.x, local_mouse.y), size=16, color=cv.Color(255, 255, 0), thickness=2)
                overlay.draw_ellipse(local_mouse, (pulse, max(10, pulse // 2)), color=cv.Color(255, 80, 80), thickness=2)
                info = f"mouse=({local_mouse.x}, {local_mouse.y})"
                overlay.draw_text(info, (20, 68), color=cv.Color(255, 255, 255), size=12)

        ov.run(updater=update, fps=60.0, duration=10.0)

    print("Overlay demo complete.")


def demo_text_overlay(duration: float, max_results: int, update_interval: float) -> None:
    _, display_index = cv.mouse_get_location_global()
    update_interval = max(0.05, float(update_interval))

    print("Live OCR text overlay demo")
    print("  - Scans the current display for all visible text")
    print("  - Draws text boxes and labels in a click-through overlay")
    print(f"  - Display: {display_index}")
    print(f"  - Max labels: {max_results}")
    print(f"  - Refresh interval: {update_interval:.2f}s")
    print(f"  - Running for {duration:.1f} seconds")

    try:
        probe = cv.capture(display_index=display_index)
        cv.find_text(probe, re.compile(r".+"), threshold=0.0)
    except RuntimeError as err:
        print("OCR dependency is missing or not ready:")
        print(err)
        return

    with cv.overlay(display_index=display_index, click_through=True) as ov:
        state = {"matches": [], "last_time": -999.0}

        def update(overlay: cv.ScreenOverlay, frame: int, elapsed: float) -> None:
            if elapsed - state["last_time"] >= update_interval:
                shot = cv.capture(display_index=display_index)
                all_hits = cv.find_text(shot, re.compile(r".+"), threshold=0.0)
                state["matches"] = all_hits[:max_results]
                state["last_time"] = elapsed

            overlay.clear()
            overlay.draw_text(
                f"OCR text: {len(state['matches'])} shown  display={display_index}",
                (16, 16),
                color=cv.Color(0, 255, 255),
                size=16,
            )

            for idx, match in enumerate(state["matches"]):
                palette = [
                    cv.Color(0, 255, 255),
                    cv.Color(255, 180, 0),
                    cv.Color(120, 255, 120),
                    cv.Color(255, 120, 180),
                ]
                color = palette[idx % len(palette)]
                label = (match.label or "text").strip()
                if len(label) > 48:
                    label = label[:45] + "..."
                overlay.draw_rect(match.rect, color=color, thickness=2, label=label)

        ov.run(updater=update, fps=30.0, duration=duration)

    print("Text overlay demo complete.")


def demo_shapes() -> None:
    img = cv.Image(900, 520)

    # Draw a rectangle.
    img.draw_rect(cv.Rect(40, 50, 220, 130), color=cv.Color(255, 255, 255), thickness=8)

    # Draw a circle.
    img.draw_ellipse(cv.Point(420, 120), 70, color=cv.Color(255, 255, 255), thickness=8)

    # Draw an X.
    x_stroke = 12
    img.draw_line(cv.Point(560, 40), cv.Point(760, 200), color=cv.Color(255, 255, 255), thickness=x_stroke)
    img.draw_line(cv.Point(760, 40), cv.Point(560, 200), color=cv.Color(255, 255, 255), thickness=x_stroke)
    x_margin = (x_stroke // 2) + 1
    x_draw_rect = cv.Rect(560 - x_margin, 40 - x_margin, (760 - 560) + 2 * x_margin, (200 - 40) + 2 * x_margin)

    # Draw a triangle.
    triangle_points = [
        cv.Point(820, 60),
        cv.Point(875, 190),
        cv.Point(765, 190),
    ]
    triangle_stroke = 10
    for i in range(len(triangle_points)):
        a = triangle_points[i]
        b = triangle_points[(i + 1) % len(triangle_points)]
        img.draw_line(a, b, color=cv.Color(255, 255, 255), thickness=triangle_stroke)
    tri_margin = (triangle_stroke // 2) + 1
    tri_min_x = min(p.x for p in triangle_points)
    tri_max_x = max(p.x for p in triangle_points)
    tri_min_y = min(p.y for p in triangle_points)
    tri_max_y = max(p.y for p in triangle_points)
    triangle_draw_rect = cv.Rect(
        tri_min_x - tri_margin,
        tri_min_y - tri_margin,
        (tri_max_x - tri_min_x) + 2 * tri_margin,
        (tri_max_y - tri_min_y) + 2 * tri_margin,
    )

    # Draw a star.
    star_points = [
        cv.Point(120, 260),
        cv.Point(145, 330),
        cv.Point(220, 332),
        cv.Point(160, 375),
        cv.Point(182, 450),
        cv.Point(120, 405),
        cv.Point(58, 450),
        cv.Point(80, 375),
        cv.Point(20, 332),
        cv.Point(95, 330),
    ]
    for i in range(len(star_points)):
        a = star_points[i]
        b = star_points[(i + 1) % len(star_points)]
        img.draw_line(a, b, color=cv.Color(255, 255, 255), thickness=8)

    # Draw an L shape (stroke style).
    img.draw_line(cv.Point(370, 260), cv.Point(370, 440), color=cv.Color(255, 255, 255), thickness=14)
    img.draw_line(cv.Point(370, 440), cv.Point(520, 440), color=cv.Color(255, 255, 255), thickness=14)

    # Draw a filled L shape for custom-template matching.
    img.draw_rect(cv.Rect(640, 280, 45, 180), color=cv.Color(255, 255, 255), thickness=-1)
    img.draw_rect(cv.Rect(640, 415, 190, 45), color=cv.Color(255, 255, 255), thickness=-1)

    detections = []
    detections.extend(cv.find_shape(img, "rectangle", threshold=0.2, min_area=900, max_results=1))
    detections.extend(cv.find_shape(img, "circle", threshold=0.12, min_area=900, max_results=1))

    # Constrain X search so triangle edges do not steal the X match.
    x_region = cv.Rect(520, 20, 260, 220)
    x_crop = img.clone()
    x_crop.crop(x_region)
    x_template = cv.shape_template(
        "custom-x",
        [
            (0.00, 0.15),
            (0.15, 0.00),
            (0.50, 0.35),
            (0.85, 0.00),
            (1.00, 0.15),
            (0.65, 0.50),
            (1.00, 0.85),
            (0.85, 1.00),
            (0.50, 0.65),
            (0.15, 1.00),
            (0.00, 0.85),
            (0.35, 0.50),
        ],
    )
    x_hits = cv.find_shape(x_crop, x_template, threshold=0.45, min_area=900, max_results=1)
    for m in x_hits:
        score_text = "0.000"
        if m.label:
            score_text = m.label.split(":")[-1]
        detections.append(
            cv.Match(
                x_draw_rect,
                label=f"center-x:{score_text}",
            )
        )

    detections.extend(cv.find_shape(img, "star", threshold=0.25, min_area=900, max_results=1))

    # Constrain triangle search to its region so it is not out-ranked by other contours.
    triangle_region = cv.Rect(740, 35, 160, 180)
    triangle_crop = img.clone()
    triangle_crop.crop(triangle_region)
    triangle_template = cv.shape_template(
        "triangle",
        [
            (0.5, 0.0),
            (1.0, 1.0),
            (0.0, 1.0),
        ],
    )
    triangle_hits = cv.find_shape(triangle_crop, triangle_template, threshold=0.45, min_area=700, max_results=1)
    for m in triangle_hits:
        score_text = "0.000"
        if m.label:
            score_text = m.label.split(":")[-1]
        detections.append(
            cv.Match(
                triangle_draw_rect,
                label=f"triangle:{score_text}",
            )
        )

    # Constrain built-in L search to center area so we capture the stroke-style L.
    center_l_region = cv.Rect(300, 220, 280, 260)
    center_crop = img.clone()
    center_crop.crop(center_l_region)
    center_l_hits = cv.find_shape(center_crop, "l", threshold=0.35, min_area=700, max_results=1)
    for m in center_l_hits:
        score_text = "0.000"
        if m.label:
            score_text = m.label.split(":")[-1]
        detections.append(
            cv.Match(
                cv.Rect(
                    center_l_region.x + m.rect.x,
                    center_l_region.y + m.rect.y,
                    m.rect.width,
                    m.rect.height,
                ),
                label=f"center-l:{score_text}",
            )
        )

    custom_l = cv.shape_template(
        "custom-l",
        [
            (0.0, 0.0),
            (0.25, 0.0),
            (0.25, 0.75),
            (1.0, 0.75),
            (1.0, 1.0),
            (0.0, 1.0),
        ],
    )
    detections.extend(cv.find_shape(img, custom_l, threshold=0.22, min_area=900, max_results=1))

    img.draw_matches(detections, color=cv.Color(0, 255, 255), thickness=2)

    out = Path("out_shapes.png")
    cv.save(img, str(out))
    print(f"Detected {len(detections)} shape(s). Saved {out.resolve()}")


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
        matches = cv.find_text(source, term, threshold=0.0)
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


def demo_elements(allow_input: bool) -> None:
    print("Screen elements demo")
    print("  1) Build a RegionElement around current mouse area")
    print("  2) Create an ImageElement from a captured probe")
    print("  3) Try TextElement search for 'File'")

    center, display_index = cv.mouse_get_location()
    region_rect = cv.Rect(max(0, center.x - 180), max(0, center.y - 120), 360, 240)
    region = cv.RegionElement(region_rect, display_index=display_index)

    local_shot = region.capture()
    local_match = region.find()
    if local_match is None:
        print("RegionElement unexpectedly had no match.")
        return

    print(f"Region center: ({local_match.rect.center().x}, {local_match.rect.center().y}) on display {display_index}")

    probe_rect = cv.Rect(max(0, region_rect.width // 4), max(0, region_rect.height // 4), 80, 40)
    probe = local_shot.clone()
    probe.crop(probe_rect)
    cv.save(probe, "out_elements_probe.png")

    image_element = cv.ImageElement(
        probe,
        threshold=0.95,
        display_index=display_index,
        search_rect=region_rect,
    )
    image_hit = image_element.find()
    print(f"ImageElement found: {image_hit is not None}")

    text_element = cv.TextElement("File", display_index=display_index)
    try:
        text_hit = text_element.find()
    except RuntimeError as err:
        print("TextElement skipped (OCR not ready):")
        print(err)
        text_hit = None

    print(f"TextElement found: {text_hit is not None}")

    screen = cv.capture(display_index=display_index)
    overlays = []
    if image_hit is not None:
        overlays.append(cv.Match(image_hit.rect, label="ImageElement"))
    if text_hit is not None:
        overlays.append(cv.Match(text_hit.rect, label=f"TextElement: {text_hit.label}"))
    overlays.append(cv.Match(region_rect, label="RegionElement"))
    screen.draw_matches(overlays, color=cv.Color(0, 255, 255), thickness=2)
    cv.save(screen, "out_elements.png")
    print("Saved out_elements.png and out_elements_probe.png")

    if not allow_input:
        print("Input actions skipped. Re-run with --allow-input to test move/click/drag.")
        return

    print("Running live element actions in 2 seconds. Keep desktop clear.")
    cv.wait(2.0)

    region.click()
    cv.wait(0.3)

    if image_hit is not None:
        image_element.move(image_hit)
        cv.wait(0.3)
        image_element.click(count=2, match=image_hit)
        cv.wait(0.3)
        image_element.drag_to(region)
        print("Executed region click + image move/double-click-via-count/drag-to-region")
    else:
        print("Skipped image element actions because no image match was found.")


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
    cv.type("hello from chivel", delay=0.03)
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


def demo_display_search(
    search_text: str,
    search_image: str,
    timeout: float,
    interval: float,
    threshold: float,
    display_indices: Optional[Sequence[int]],
) -> None:
    search_target: object
    target_desc: str

    if search_image.strip():
        loaded = cv.load(search_image.strip())
        if not isinstance(loaded, cv.Image):
            print(f"Could not load image template: {search_image}")
            return
        search_target = loaded
        target_desc = f"image={search_image}"
    else:
        search_target = search_text
        target_desc = f"text={search_text!r}"

    print("Display search demo")
    print(f"  Target: {target_desc}")
    print(f"  Timeout: {timeout:.2f}s")
    print(f"  Poll interval: {interval:.2f}s")
    if display_indices is None:
        print("  Displays: primary-first scan")
    else:
        print(f"  Displays: {list(display_indices)}")

    start = time.time()
    found_now = cv.find_display(search_target, threshold=threshold, display_indices=display_indices)
    elapsed_now = time.time() - start
    if found_now >= 0:
        print(f"find_display -> display {found_now} ({elapsed_now:.3f}s)")
    else:
        print(f"find_display -> not found ({elapsed_now:.3f}s)")

    start = time.time()
    found_wait = cv.expect_display(
        search_target,
        interval=interval,
        timeout=timeout,
        threshold=threshold,
        display_indices=display_indices,
    )
    elapsed_wait = time.time() - start
    if found_wait >= 0:
        print(f"expect_display -> display {found_wait} ({elapsed_wait:.3f}s)")
    else:
        print(f"expect_display -> -1 timeout ({elapsed_wait:.3f}s)")


def main() -> None:
    parser = argparse.ArgumentParser(description="CHIVEL sandbox runner")
    parser.add_argument(
        "demo",
        choices=["image", "overlay", "text-overlay", "shapes", "capture", "match", "ocr", "elements", "input", "record", "play", "mouse-display", "display-search", "step-record", "step-play"],
        help="Which demo to run",
    )
    parser.add_argument(
        "--allow-input",
        action="store_true",
        help="Allow real mouse/keyboard input events for the input demo",
    )
    parser.add_argument(
        "--text-duration",
        type=float,
        default=15.0,
        help="Duration in seconds for the text-overlay demo",
    )
    parser.add_argument(
        "--text-max-results",
        type=int,
        default=120,
        help="Maximum number of OCR text boxes to draw in text-overlay demo",
    )
    parser.add_argument(
        "--text-update-interval",
        type=float,
        default=0.35,
        help="OCR refresh interval in seconds for text-overlay demo",
    )
    parser.add_argument(
        "--display-search-text",
        default="Settings",
        help="Text to search for in display-search demo (ignored when --display-search-image is set)",
    )
    parser.add_argument(
        "--display-search-image",
        default="",
        help="Image path to search for in display-search demo",
    )
    parser.add_argument(
        "--display-search-timeout",
        type=float,
        default=10.0,
        help="Timeout in seconds for expect_display in display-search demo",
    )
    parser.add_argument(
        "--display-search-interval",
        type=float,
        default=0.25,
        help="Polling interval in seconds for expect_display in display-search demo",
    )
    parser.add_argument(
        "--display-search-threshold",
        type=float,
        default=0.8,
        help="Image threshold for display-search demo",
    )
    parser.add_argument(
        "--display-indices",
        default="",
        help="Comma-separated display indices to scan/order for display-search demo (example: 1,0)",
    )

    args = parser.parse_args()

    if args.demo == "image":
        demo_image()
    elif args.demo == "overlay":
        demo_overlay()
    elif args.demo == "text-overlay":
        demo_text_overlay(
            duration=max(1.0, args.text_duration),
            max_results=max(1, args.text_max_results),
            update_interval=max(0.05, args.text_update_interval),
        )
    elif args.demo == "shapes":
        demo_shapes()
    elif args.demo == "capture":
        demo_capture()
    elif args.demo == "match":
        demo_match()
    elif args.demo == "ocr":
        demo_ocr()
    elif args.demo == "elements":
        demo_elements(args.allow_input)
    elif args.demo == "input":
        demo_input(args.allow_input)
    elif args.demo == "record":
        demo_record()
    elif args.demo == "play":
        demo_play(args.allow_input)
    elif args.demo == "mouse-display":
        demo_mouse_display()
    elif args.demo == "display-search":
        demo_display_search(
            search_text=args.display_search_text,
            search_image=args.display_search_image,
            timeout=max(0.0, args.display_search_timeout),
            interval=max(0.01, args.display_search_interval),
            threshold=max(0.0, min(1.0, args.display_search_threshold)),
            display_indices=_parse_display_indices(args.display_indices),
        )
    elif args.demo == "step-record":
        demo_step_record()
    elif args.demo == "step-play":
        demo_step_play(args.allow_input)


if __name__ == "__main__":
    main()
