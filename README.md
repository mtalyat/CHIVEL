# CHIVEL

<img src='Images/icon.png' alt='The CHIVEL logo.' width=200px>

CHIVEL, previously known as CHISL (Computer-Human Interaction Scripting Language) is a Python extension meant for controlling your device. It provides simple interfaces for finding things on the screen, controlling the keyboard/mouse, and more. This was originally its own scripting language, but was later revamped into a Python module, so that the powerful features of Python could be used in addition to the computer vision that CHIVEL provides. The project was also renamed from CHISL to CHIVEL, as CHISL was already taken on PyPI.

## How To Use

Install the Python [module](https://pypi.org/project/chivel/):

    pip install chivel

Import chivel and have at it!

## Example

    import chivel
    x = chivel.load("x.png")
    screen = chivel.capture(0)
    matches = chivel.find(screen, x)
    for match in matches:
        chivel.mouse_move(match)
        chivel.mouse_click()
