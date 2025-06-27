import chivel
import os
os.chdir(os.path.dirname(os.path.abspath(__file__)))

screen = chivel.capture(1)
mask = screen.clone()
mask.range((128, 128, 128), (255, 255, 255))
screen.mask(mask)
screen.threshold()
screen.show()