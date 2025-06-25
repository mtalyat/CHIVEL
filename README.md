# CHIVEL

CHIVEL, previously known as CHISL (Computer-Human Interaction Scripting Language) is a Python extension meant for controlling your device. It provides simple interfaces for finding things on the screen, controlling the keyboard/mouse, and more. This was originally its own scripting language, but was later revamped into a Python module, so that the powerful features of Python could be used in addition to the computer vision that CHIVEL provides. The project was also renamed from CHISL to CHIVEL, as CHISL was already taken on PyPI...

### Example

    import chivel
    x = chivel.load("x.png")
    screen = chivel.capture(0)
    matches = chivel.find(screen, x)
    chivel.mouse_move(matches[0])
    chivel.mouse_click()

## Technologies
C++, OpenCV, and Tesseract OCR.

Currently only supported for Windows.
