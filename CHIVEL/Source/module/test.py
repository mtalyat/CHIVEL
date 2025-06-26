import chivel

# set cwd to the directory containing this file
import os
os.chdir(os.path.dirname(os.path.abspath(__file__)))

print("Recording started. Press F12 to stop.")
chivel.record('recording.py')
print("Recording stopped.")