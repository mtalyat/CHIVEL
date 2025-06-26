import chivel

# set cwd to the directory containing this file
import os
os.chdir(os.path.dirname(os.path.abspath(__file__)))

# print("Recording started. Press F12 to stop.")
# chivel.record('recording.py')
# print("Recording stopped.")

display, x, y = chivel.mouse_get_location()
print(f"Mouse is at display {display}, coordinates ({x}, {y})")
screen = chivel.capture(display, (x - 50, y - 50, 100, 100))
chivel.show(screen, "Captured Area")