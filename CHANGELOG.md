# Change log

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