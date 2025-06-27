import chivel
import os
os.chdir(os.path.dirname(os.path.abspath(__file__)))

screen = chivel.capture(0)
size = screen.get_size()
print(f"Captured screen size: {size[0]}x{size[1]}")
print(f"Display rect: {chivel.display_get_rect(0)}")
chivel.mouse_move(0, (10, 10))
chivel.wait(1.0)
print(f'Mouse moved to {chivel.mouse_get_location()}')
chivel.mouse_move(0, (size[0] - 10, size[1] - 10))
chivel.wait(1.0)
print(f'Mouse moved to {chivel.mouse_get_location()}')