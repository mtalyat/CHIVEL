import chivel
import os
os.chdir(os.path.dirname(os.path.abspath(__file__)))

screen = chivel.capture(1)
matches = chivel.find(screen, ".*", 0.0, chivel.TEXT_LINE)
screen.draw_matches(matches)
screen.show("Matches")