# Change log

## 0.8.1
- Add input for key and mouse inputs.

## 0.8.0
- Add missing keys.
- Fix syntax errors.

## 0.7.8
- Fix syntax errors.

## 0.7.7
- Change KEY_ESC to KEY_ESCAPE.

## 0.7.6
- Replace clipboard code with pyperclip.

## 0.7.5
- Small code reorg.

## 0.7.4
- Fix syntax error.

## 0.7.3
- Add get_clipboard and set_clipboard.

## 0.7.2
- Add check_for() in order to check for buttons/keys being pressed down for any instance in time.
- Added an optional timeout for wait_for().
- wait_for() now also accepts buttons.
- Fixed argument name inconsistencies.

## 0.7.1
- Add pause() method which will pause execution and wait for input in the terminal.
- Rects now have a center() function.
- You can now pass rects into mouse_move(), etc.

## 0.7.0
- Rework entire library to use raw Python to help avoid Python version dependencies.
- Changed OCR library to rapidocr-onnxruntime.
- Add primary display setter/getter. This makes capture(), mouse_move(), etc. relative to the given display.
- Add regex support for text finding.
- Rework recording:
    - Now uses json and pngs for storage.
    - Allow for "steps" to be taken.
- Add playback function, play(), for recordings.
- Add wait_for key function.
- Many functions now allow for multiple keys to be passed in to do at the same time, such as key_click().
- Add image show function.
- General improvements.

## 0.6.3
- Fix find_image.

## 0.6.2
- Fix expect_any function return object.

## 0.6.1
- Fix PyPI upload.

## 0.6.0
- Fix dependencies.
- Split find into find_image and find_text.
- Add KEY_META, which refers to the Windows key.
- Add find_any, find_all, expect_any, expect_all.
- Update load function to be able to load text, images, or a list of files.
- Update type to allow for a list of strings, KEY_ values, or float values (for additional wait times).

## 0.5.0
- Create Point, Rect, Color, and Match types.
    - Replace existing function arguments with the appropriate types.
- Rename to_color to convert.

## 0.4.1
- Add tessdata folder to build.

## 0.4.0
- Fix DPI scaling.
- Fix Python3 dependency.
- Fix display_index functions.
- Find now returns the text found (in addition to location), when searching for text.
- Reorder values from mouse_get_location (now position, then display_index).
- Move chivel.show to Image.
- Remove chivel.draw. Replaced it with methods found in Image.
- Add the following functions to Image:
    - clone
    - crop
    - grayscale
    - scale
    - rotate
    - flip
    - resize
    - draw_rect
    - draw_matches
    - draw_line
    - draw_text
    - draw_ellipse
    - draw_image
    - invert
    - brightness
    - contrast
    - sharpen
    - blur
    - threshold
    - normalize
    - edge
    - emboss
    - split
    - merge
    - to_color
    - range
    - mask
- Add the following constants:
    - BUTTON_LEFT
    - BUTTON_RIGHT
    - BUTTON_MIDDLE
    - BUTTON_X1
    - BUTTON_X2
    - FLIP_VERTICAL
    - FLIP_HORIZONTAL
    - FLIP_BOTH
    - COLOR_SPACE_UNKNOWN
    - COLOR_SPACE_BGR
    - COLOR_SPACE_BGRA
    - COLOR_SPACE_RGB
    - COLOR_SPACE_RGBA
    - COLOR_SPACE_GRAY
    - COLOR_SPACE_HSV

## 0.3.2
- Update project descriptions.

## 0.3.1
- Add play.
- Remove debug lines.

## 0.3.0
- Add recording.
- Add recording simplification.
- Add capture rect.
- Add get mouse display.
- Add get mouse location.
- Add get display rect (relative to primary display).

## 0.2.0
- Add KEY constants.
- Add key_click, key_down, key_up.
- Improve find for text searches.
    - Now uses Regex patterns.

## 0.1.7
- Fix DISPLAY_COUNT constant.

## 0.1.6
- Add DISPLAY_COUNT constant.

## 0.1.5
- Actually fix module structure.

## 0.1.4
- Attempt fix module structure.

## 0.1.3
- Attempt to fix module structure.

## 0.1.2
- Attempt to fix module structure.

## 0.1.1
- Fix mouse button indexing.

## 0.1.0
- Initial release.