import chivel

# set cwd to the directory containing this file
import os
os.chdir(os.path.dirname(os.path.abspath(__file__)))

print(chivel.DISPLAY_COUNT)

screen = chivel.capture()
image = chivel.load('test.png')
matches = chivel.find(screen, image)
if matches:
    print(f"Found {len(matches)} matches:")
    for match in matches:
        print(f"Match at {match['x']}, {match['y']} with confidence {match['confidence']}")
else:
    print("No matches found.")