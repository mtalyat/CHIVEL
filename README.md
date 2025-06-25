# CHIVEL

<<<<<<< Updated upstream
Check out the [Wiki](https://github.com/mtalyat/Computer-Human-Interface-Scripting-Language/wiki) for an installation guide, and more.

## Features
- Easy to learn.
- Human readable scripting language.
- Control the keyboard and mouse.
- Identify and act upon elements and text on the screen.

## Code

### Comments
| Command | Description |
|---|---|
| `# This is a comment.` | Single-line comment. |
| `#- This is a comment. -#` | Multi-line/inline comment. |

### Variables
| Command | Description |
|---|---|
| `Set <var> to <value>.` | Sets a variable to a value. |
| `Load <var> from <path>.` | Load a file from the path into the variable. |
| `Save <var> to <path>.` | Saves a variable to the disk. |
| `Delete <var>.` | Deletes the variable with the var. |
| `Delete at <path>.` | Deletes the file or directory at `path`. |
| `Copy <var> to <destination>.` | Copies the value of `variable` to `var`. |
| `Get <var> from <collection> at <index>.` | Gets the value from a collection at the `index` and stores it in `var`. |
| `Count <var> from <collection>.` | Stores the size of `collection` in `var`. |

### Images
| Command | Description |
|---|---|
| `Capture <var>.` | Captures all of the screen and stores it in `var`. |
| `Capture <var> at <x> <y> <width> <height>.` | Captures part of the screen and stores it in `var`. |
| `Crop <image> at <x> <y> <width> <height>.` | Crops the image in `var` to the x y width height. |
| `Find <var> by <template> in <image>.` | Finds the best match of `template` within `image` equal to or above the default threshold. |
| `Find <var> by <template> in <image> with <threshold>.` | Finds the best match of `template` within `image` equal to or above the given threshold. |
| `Find all <var> by <template> in <image>.` | Finds all possible matches equal to or above the default threshold. |
| `Find all <var> by <template> in <image> with <threshold>.` | Finds all possible matches equal to or above the given threshold. |
| `Find text <block/paragraph/symbol/line/word> <var> by <template> in <image>.` | Finds the best match of text `template` within `image` equal to or above the default threshold. |
| `Find text <block/paragraph/symbol/line/word> <var> by <template> in <image> with <threshold>.` | Finds the best match of text `template` within `image` equal to or above the given threshold. |
| `Find all text <block/paragraph/symbol/line/word> <var> by <template> in <image>.` | Finds all possible text matches equal to or above the default threshold. |
| `Find all text <block/paragraph/symbol/line/word> <var> by <template> in <image> with <threshold>.` | Finds all possible text matches equal to or above the given threshold. |
| `Read <var> from <image>.` | Reads all of the text in `image`. |
| `Draw <match> on <image>.` | Draws an outline of `match` onto `image`. |
| `Draw <x> <y> <w> <h> on <image>.` | Draws a rectangle into `image`. |

### Utility
| Command | Description |
|---|---|
| `Wait <time> <ms/s/m/h>.` | Pauses for the given amount of time. |
| `Countdown <time> <ms/s/m/h>.` | Pauses for the given amount of time, and shows the time left in real time. |
| `Pause.` | Pauses until the user hits a key. |
| `Print <value>.` | Prints the text or variable to the console. |
| `Print <value> in <black/red/green/yellow/blue/magenta/cyan/white>.` | Prints the text or variable to the console in the foreground color. |
| `Print <value> in <black/red/green/yellow/blue/magenta/cyan/white> with <black/red/green/yellow/blue/magenta/cyan/white>.` | Prints the text or variable to the console in the foreground color with the background color. |
| `Show <value>.` | Shows the text or variable, and pauses for input. |
| `Open <path>.` | Opens a file or directory. |
| `Input to <var>.` | Reads input from the user. |
| `Input <prompt> to <var>.` | Reads input from the user with an inline prompt. |

### Mouse
| Command | Description |
|---|---|
| `Move mouse to <x> <y>.` | Sets the mouse to the position. |
| `Move mouse to <match>.` | Sets the mouse to the match's center position. |
| `Move mouse by <x> <y>.` | Moves the mouse relative to its current position. |
| `Press mouse <button=left>.` | Sends a mouse button press event to the OS. |
| `Release mouse <button=left>.` | Sends a mouse button release event to the OS. |
| `Click mouse <button=left>.` | Sends a mouse down and mouse up event to the OS. |
| `Click mouse <button=left> <times> times.` | Sends multiple mouse down and mouse up event to the OS. |
| `Scroll mouse by <y> <x=0>.` | Sends a mouse scroll event to the OS. |

### Keyboard
| Command | Description |
|---|---|
| `Press key <key>.` | Sends a key down event to the OS. |
| `Release key <key>.` | Sends a key up event to the OS. |
| `Type <value>.` | Sends a series of key up/downs of the phrase or variable to the OS with no delay. |
| `Type <value> with <delay> <ms/s/m/h> delay.` | Sends a series of key up/downs of the phrase or variable to the OS. `delay` is the wait time between each character. |

### Control
| Command | Description |
|---|---|
| `Label <label>.` | Creates a label that can be gone to with a goto statement. |
| `Goto <label>.` | Starts executing at the given `label`. |
| `Goto <label> if <condition>.` | Goes to the label only if the `condition` is true. |
| `Exit.` | Quits the program. |

### Scripting
| Command | Description |
|---|---|
| `Record to <path>.` | Records keyboard and mouse actions taken to a file at the given path. |
| `Run <var>.` | Runs a script loaded into a variable. |

### Configuration
| Command | Description |
|---|---|
| `Configure <setting> to <value>.` | Turns echoing commands on or off. |

### Testing
| Command | Description |
|---|---|
| `Test <test> expect <expected>.` | Runs a test and records the results. |

### Built-in

There are some constants that are built into CHISL and controlled by the program.

| Constant | Description |
|---|---|
| `OUTPUT` | Holds the string of the output from the most recently ran command, if applicable. |
| `RESULT` | Holds the string of the result from the most recently ran command, if applicable. |
| `PASS_COUNT` | The number of passes from `Test` commands that have been ran. |
| `FAIL_COUNT` | The number of fails from `Test` commands that have been ran. |
=======
CHIVEL, previously known as CHISL (Computer-Human Interaction Scripting Language) is a Python extension meant for controlling your device. It provides simple interfaces for finding things on the screen, controlling the keyboard/mouse, and more. This was originally its own scripting language, but was later revamped into a Python module, so that the powerful features of Python could be used in addition to the computer vision that CHIVEL provides. The project was also renamed from CHISL to CHIVEL, as CHISL was already taken on PyPI...
>>>>>>> Stashed changes

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
