import time
import threading
from pynput import mouse, keyboard
import chivel

# Map pynput key to Windows virtual-key code
def key_to_vk(key):
    # Handle special keys
    if hasattr(key, 'vk'):
        return key.vk
    if hasattr(key, 'value') and hasattr(key.value, 'vk'):
        return key.value.vk
    # Fallback for character keys
    try:
        return ord(key.char.upper())
    except Exception:
        return None
    
ACTION_NONE = 0
ACTION_MOUSE_MOVE = 1
ACTION_MOUSE_CLICK = 2
ACTION_MOUSE_DOWN = 3
ACTION_MOUSE_UP = 4
ACTION_MOUSE_SCROLL = 5
ACTION_KEY_CLICK = 6
ACTION_KEY_DOWN = 7
ACTION_KEY_UP = 8
ACTION_TYPE = 9

SIMPLIFY_MOVE = 1
SIMPLIFY_CLICK = 2
SIMPLIFY_TYPE = 4
SIMPLIFY_TIME = 8

TIME_CLICK = 0.5 # threshold for a click in seconds
TIME_TYPE = 1.0 # threshold for typing in seconds
TIME_PAUSE = 1.0 # time to wait between actions in seconds
TIME_ROUND = 0.250 # time to round to for actions in seconds

def record(output_path, simplify, stop_key=123):  # 123 is F12
    """
    Record mouse and keyboard actions until stop_key (virtual-key code) is pressed.
    Writes a Python script using the chivel library to replay the actions.
    """
    actions = []
    start_time = time.time()
    stop_flag = threading.Event()

    def now():
        return time.time() - start_time

    def on_move(x, y):
        display_index = chivel.mouse_get_display()
        display_rect = chivel.display_get_rect(display_index)
        actions.append((now(), ACTION_MOUSE_MOVE, (display_index, x - display_rect[0], y - display_rect[1])))

    def on_click(x, y, button, pressed):
        if pressed:
            actions.append((now(), ACTION_MOUSE_DOWN, (x, y, button.name)))
        else:
            actions.append((now(), ACTION_MOUSE_UP, (x, y, button.name)))

    def on_scroll(x, y, dx, dy):
        actions.append((now(), ACTION_MOUSE_SCROLL, (dx, dy)))

    def on_press(key):
        vk = key_to_vk(key)
        actions.append((now(), ACTION_KEY_DOWN, (vk, key.char if hasattr(key, 'char') else None)))
        if vk == stop_key:
            stop_flag.set()
            return False

    def on_release(key):
        vk = key_to_vk(key)
        actions.append((now(), ACTION_KEY_UP, (vk, key.char if hasattr(key, 'char') else None)))

    mouse_listener = mouse.Listener(on_move=on_move, on_click=on_click, on_scroll=on_scroll)
    keyboard_listener = keyboard.Listener(on_press=on_press, on_release=on_release)
    mouse_listener.start()
    keyboard_listener.start()

    stop_flag.wait()
    mouse_listener.stop()
    keyboard_listener.stop()

    if simplify > 0:
        simplify_move = (simplify & SIMPLIFY_MOVE) != 0
        simplify_click = (simplify & SIMPLIFY_CLICK) != 0
        simplify_type = (simplify & SIMPLIFY_TYPE) != 0
        simplify_time = (simplify & SIMPLIFY_TIME) != 0
        oldActions = actions
        actions = []
        actions.append((0, ACTION_NONE, ()))
        
        # handles the simplification of actions
        def add_action(t, action, params):
            oldT, oldAction, oldParams = actions[-1]
            if simplify_move and action == ACTION_MOUSE_MOVE and oldAction == ACTION_MOUSE_MOVE and params[0] == oldParams[0]:
                # if another move, extend the move
                actions.pop()
                add_action(t, ACTION_MOUSE_MOVE, (params[0], params[1], params[2]))
            elif simplify_click and action == ACTION_MOUSE_UP and oldAction == ACTION_MOUSE_DOWN and params[2] == oldParams[2] and t - oldT <= TIME_CLICK:
                # if mouse up after mouse down, replace with click
                actions.pop()
                add_action(t, ACTION_MOUSE_CLICK, (params[0], params[1], params[2], 1))
            elif simplify_click and action == ACTION_MOUSE_CLICK and oldAction == ACTION_MOUSE_CLICK and params[2] == oldParams[2] and t - oldT <= TIME_CLICK:
                # if another click, extend the click count
                actions.pop()
                add_action(t, ACTION_MOUSE_CLICK, (params[0], params[1], params[2], params[3] + 1))
            elif simplify_click and action == ACTION_KEY_UP and oldAction == ACTION_KEY_DOWN and params[0] == oldParams[0] and t - oldT <= TIME_CLICK:
                # if key up after key down, replace with click
                actions.pop()
                add_action(t, ACTION_KEY_CLICK, (params[0], params[1], 1))
            elif simplify_click and action == ACTION_KEY_CLICK and params[1] is not None and oldAction == ACTION_KEY_CLICK and params[0] == oldParams[0] and t - oldT <= TIME_CLICK:
                # if another key click, extend the click count if the same type, or start typing if different
                actions.pop()
                add_action(t, ACTION_KEY_CLICK, (params[0], params[1], oldParams[2] + 1))
            elif simplify_type and action == ACTION_KEY_CLICK and params[1] is not None and oldAction == ACTION_KEY_CLICK and params[0] != oldParams[0] and t - oldT <= TIME_CLICK:
                # if another key click, extend the click count if the same type, or start typing if different
                actions.pop()
                add_action(t, ACTION_TYPE, (oldParams[1] + params[1]))
            elif simplify_type and action == ACTION_KEY_CLICK and params[1] is not None and oldAction == ACTION_TYPE and t - oldT <= TIME_TYPE:
                # extend typing if within threshold
                actions.pop()
                add_action(t, ACTION_TYPE, (oldParams[0] + params[1]))
            else:
                # otherwise, add a new action
                actions.append((t, action, params))
        
        # re-add each action and simplify as needed
        for t, action, params in oldActions:
            add_action(t, action, params)

        # simplify the times
        if simplify_time:
            last_time = 0
            last_action = ACTION_NONE
            for i in range(len(actions)):
                t, action, params = actions[i]
                if action == ACTION_NONE:
                    continue
                if t - last_time > TIME_PAUSE:
                    # shorten the time if too long since last action
                    actions[i] = (last_time + TIME_PAUSE, action, params)
                elif last_action == ACTION_NONE or last_action == ACTION_MOUSE_MOVE:
                    # no time after mouse move or if no previous action
                    actions[i] = (round(last_time / TIME_ROUND) * TIME_ROUND, action, params)
                else:
                    # round the time to the nearest TIME_ROUND
                    dt = t - last_time
                    actions[i] = (last_time + (round(dt / TIME_ROUND) * TIME_ROUND), action, params)
                last_time = actions[i][0]
                last_action = action

        # remove any ACTION_NONE entries
        actions = [a for a in actions if a[1] != ACTION_NONE]

    with open(output_path, 'w', encoding='utf-8') as f:
        f.write("import chivel as c\n\n")
        f.write("def play():\n")
        last_time = 0
        for t, action, params in actions:
            if action == ACTION_NONE:
                continue
            wait = t - last_time - 0.001
            if wait >= 0.001:
                f.write(f"    c.wait({wait:.3f})\n")
            if action == ACTION_MOUSE_MOVE:
                f.write(f"    c.mouse_move({params[0]}, ({params[1]}, {params[2]}))\n")
            elif action == ACTION_MOUSE_DOWN:
                btn = 0 if params[2] == 'left' else 1 if params[2] == 'right' else 2
                f.write(f"    c.mouse_down({btn})\n")
            elif action == ACTION_MOUSE_UP:
                btn = 0 if params[2] == 'left' else 1 if params[2] == 'right' else 2
                f.write(f"    c.mouse_up({btn})\n")
            elif action == ACTION_MOUSE_CLICK:
                btn = 0 if params[2] == 'left' else 1 if params[2] == 'right' else 2
                f.write(f"    c.mouse_click({btn})\n")
            elif action == ACTION_MOUSE_SCROLL:
                f.write(f"    c.mouse_scroll({params[1]}, {params[0]})\n")
            elif action == ACTION_KEY_DOWN:
                if params[0] != stop_key:
                    f.write(f"    c.key_down({params[0]})\n")
            elif action == ACTION_KEY_UP:
                if params[0] != stop_key:
                    f.write(f"    c.key_up({params[0]})\n")
            elif action == ACTION_KEY_CLICK:
                if params[0] != stop_key:
                    f.write(f"    c.key_click({params[0]}, count={params[2]})\n")
            elif action == ACTION_TYPE:
                text = params[0].replace("'", "\\'").replace('\n', '\\n')
                f.write(f"    c.type('{text}', wait=0.05)\n")
            last_time = t
        f.write("\nif __name__ == '__main__':\n    play()\n")
