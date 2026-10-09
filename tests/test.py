"""Tests for CHIVEL. Run with `python test.py`.
"""

from __future__ import annotations
import chivel as cv


def main():
    image = cv.load('test2.png')
    if not isinstance(image, cv.Image):
        return
    element = cv.ImageElement(image)
    if element.click():
        cv.wait(1)
        element = cv.TextElement('Command')
        element.click()
    pass

if __name__ == "__main__":
    main()
