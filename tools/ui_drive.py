#!/usr/bin/env python3
"""Drive the MoleHole window started by `launch` on Hyprland: resize, click, type, screenshot, liveness check.

Commands: launch [args], resize W H, tile, click X Y [button] [count], drag X1 Y1 X2 Y2, scroll X Y N, key chord..., shot PATH,
alive, info, kill.
`launch` records PID and window address in .gpp/ui_drive.json; every command targets that instance (matched by PID), so
concurrent --export-image runs are never touched. `resize` makes the window floating and verifies the size; `tile` undoes that.
Pointer is positioned with hyprctl, buttons/keys/wheel are injected through /dev/uinput (needs the `input` group).
Coordinates are window-relative pixels.
"""
import datetime
import fcntl
import json
import os
import re
import signal
import struct
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOG = os.path.join(ROOT, ".gpp", "run.log")
STATE = os.path.join(ROOT, ".gpp", "ui_drive.json")

EV_SYN, EV_KEY, EV_REL = 0, 1, 2
REL_X, REL_Y, REL_WHEEL = 0, 1, 8
BTN = {"left": 272, "right": 273, "middle": 274}
KEYS = {
    "esc": 1, "1": 2, "2": 3, "3": 4, "4": 5, "5": 6, "6": 7, "7": 8, "8": 9, "9": 10, "0": 11, "backspace": 14, "tab": 15,
    "q": 16, "w": 17, "e": 18, "r": 19, "t": 20, "y": 21, "u": 22, "i": 23, "o": 24, "p": 25, "enter": 28, "ctrl": 29,
    "a": 30, "s": 31, "d": 32, "f": 33, "g": 34, "h": 35, "j": 36, "k": 37, "l": 38, "shift": 42,
    "z": 44, "x": 45, "c": 46, "v": 47, "b": 48, "n": 49, "m": 50, "alt": 56, "space": 57, ".": 52, "-": 12, "delete": 111,
    "f1": 59, "f2": 60, "f3": 61, "f4": 62, "f5": 63, "up": 103, "left": 105, "right": 106, "down": 108,
    "home": 102, "end": 107, "pageup": 104, "pagedown": 109, "/": 53,
}
KEYS["escape"] = KEYS["esc"]
CHORDS = {}
if os.environ.get("UI_DRIVE_LAYOUT", "de") == "de":
    KEYS["y"], KEYS["z"] = KEYS["z"], KEYS["y"]
    KEYS["-"] = 53
    CHORDS["/"] = [KEYS["shift"], 8]
KEYS["return"] = KEYS["enter"]
KEYS["del"] = KEYS["delete"]


def hypr(*args):
    return subprocess.run(["hyprctl", *args], capture_output=True, text=True, check=True).stdout


def load_state():
    try:
        with open(STATE) as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def pid_alive(pid):
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    try:
        with open(f"/proc/{pid}/stat") as f:
            return f.read().rsplit(")", 1)[1].split()[0] != "Z"
    except OSError:
        return False


def window():
    state = load_state()
    if not state:
        sys.exit("no launched instance (run `launch`)")
    if not pid_alive(state["pid"]):
        sys.exit(f"launched instance (pid {state['pid']}) is not running")
    for client in json.loads(hypr("clients", "-j")):
        if client["pid"] == state["pid"]:
            return client
    sys.exit(f"no window for pid {state['pid']}")


def dispatch(expr):
    hypr("dispatch", expr)


def wait_window(timeout=60):
    end = time.time() + timeout
    while time.time() < end:
        try:
            return window()
        except SystemExit as e:
            if "not running" in str(e):
                raise
            time.sleep(0.5)
    sys.exit("window did not appear")


class Uinput:
    def __init__(self):
        self.fd = os.open("/dev/uinput", os.O_WRONLY | os.O_NONBLOCK)
        fcntl.ioctl(self.fd, 0x40045564, EV_KEY)
        fcntl.ioctl(self.fd, 0x40045564, EV_REL)
        for code in [*BTN.values(), *KEYS.values()]:
            fcntl.ioctl(self.fd, 0x40045565, code)
        for code in (REL_X, REL_Y, REL_WHEEL):
            fcntl.ioctl(self.fd, 0x40045566, code)
        setup = struct.pack("HHHH80sI", 0x03, 0x1234, 0x5678, 1, b"molehole-drive", 0)
        fcntl.ioctl(self.fd, 0x405C5503, setup)
        fcntl.ioctl(self.fd, 0x5501)
        time.sleep(0.6)

    def emit(self, kind, code, value):
        os.write(self.fd, struct.pack("llHHi", 0, 0, kind, code, value))
        os.write(self.fd, struct.pack("llHHi", 0, 0, EV_SYN, 0, 0))

    def close(self):
        fcntl.ioctl(self.fd, 0x5502)
        os.close(self.fd)


def move(x, y):
    win = window()
    dispatch(f"hl.dsp.cursor.move({{x={win['at'][0] + x},y={win['at'][1] + y}}})")
    time.sleep(0.3)


def cmd_launch(extra):
    state = load_state()
    if state and pid_alive(state["pid"]):
        sys.exit(f"already running (pid {state['pid']}); `kill` first")
    os.makedirs(os.path.dirname(LOG), exist_ok=True)
    binary = os.environ.get("MOLEHOLE_BIN") or os.path.join(ROOT, "build", "MoleHole")
    proc = subprocess.Popen([binary, *extra], cwd=os.environ.get("MOLEHOLE_CWD") or ROOT, stdout=open(LOG, "w"),
                            stderr=subprocess.STDOUT, start_new_session=True)
    with open(STATE, "w") as f:
        json.dump({"pid": proc.pid}, f)
    win = wait_window()
    with open(STATE, "w") as f:
        json.dump({"pid": proc.pid, "address": win["address"]}, f)
    print(json.dumps(win["size"]))
    time.sleep(4)


def cmd_resize(w, h):
    win = window()
    target = f"window='address:{win['address']}'"
    if win.get("fullscreen"):
        dispatch(f"hl.dsp.window.fullscreen({{action='unset',{target}}})")
        time.sleep(0.6)
        win = window()
    if not win["floating"]:
        dispatch(f"hl.dsp.window.float({{action='enable',{target}}})")
    monitor = next(m for m in json.loads(hypr("monitors", "-j")) if m["id"] == win["monitor"])
    for _ in range(5):
        dispatch(f"hl.dsp.window.resize({{x={w},y={h},relative=false,{target}}})")
        dispatch(f"hl.dsp.window.move({{x={monitor['x']},y={monitor['y']},relative=false,{target}}})")
        time.sleep(0.6)
        win = window()
        if win["size"] == [w, h]:
            print(json.dumps(win["size"]))
            return
    sys.exit(f"resize failed: wanted {w}x{h}, got {win['size'][0]}x{win['size'][1]}")


def cmd_tile():
    win = window()
    if win["floating"]:
        dispatch(f"hl.dsp.window.float({{action='disable',window='address:{win['address']}'}})")
        time.sleep(0.6)
    print(json.dumps(window()["size"]))


def cmd_click(x, y, button="left", count=1):
    dev = Uinput()
    cmd_click_focus(x, y, dev)
    for _ in range(count):
        dev.emit(EV_KEY, BTN[button], 1)
        time.sleep(0.05)
        dev.emit(EV_KEY, BTN[button], 0)
        time.sleep(0.08)
    dev.close()


def cmd_drag(x1, y1, x2, y2, steps=20):
    dev = Uinput()
    cmd_click_focus(x1, y1, dev)
    dev.emit(EV_KEY, BTN["left"], 1)
    for i in range(1, steps + 1):
        move(x1 + (x2 - x1) * i / steps, y1 + (y2 - y1) * i / steps)
    dev.emit(EV_KEY, BTN["left"], 0)
    dev.close()


def cmd_scroll(x, y, notches):
    dev = Uinput()
    cmd_click_focus(x, y, dev)
    for _ in range(abs(notches)):
        dev.emit(EV_REL, REL_WHEEL, 1 if notches > 0 else -1)
        time.sleep(0.05)
    dev.close()


def cmd_click_focus(x, y, dev):
    win = window()
    dispatch(f"hl.dsp.focus({{window='address:{win['address']}'}})")
    move(x, y)
    dev.emit(EV_REL, REL_X, 1)
    dev.emit(EV_REL, REL_X, -1)
    time.sleep(0.25)


def cmd_key(*names):
    dev = Uinput()
    dispatch(f"hl.dsp.focus({{window='address:{window()['address']}'}})")
    for chord in names:
        codes = [code for part in chord.split("+") for code in CHORDS.get(part, [KEYS[part]])]
        for code in codes:
            dev.emit(EV_KEY, code, 1)
        time.sleep(0.05)
        for code in reversed(codes):
            dev.emit(EV_KEY, code, 0)
        time.sleep(0.08)
    dev.close()


def cmd_shot(path):
    win = window()
    x, y = win["at"]
    w, h = win["size"]
    subprocess.run(["grim", "-g", f"{x},{y} {w}x{h}", path], check=True)
    print(path)


def cmd_alive(max_age=5.0):
    last = None
    with open(LOG, errors="replace") as log:
        for line in log:
            if "Render stats: UI" in line:
                last = line
    if not last:
        sys.exit("DEAD: no UI frame stats in log")
    stamp = datetime.datetime.strptime(re.match(r"\[([\d\- :.]+)\]", last).group(1), "%Y-%m-%d %H:%M:%S.%f")
    age = (datetime.datetime.now() - stamp).total_seconds()
    if age > max_age:
        sys.exit(f"HUNG: last UI frame stats {age:.1f}s ago")
    print(f"alive: {last.split('Render stats: ')[1].strip()}")


def cmd_kill():
    state = load_state()
    if not state:
        print("nothing to kill: no launched instance recorded")
        return
    pid = state["pid"]
    if pid_alive(pid):
        os.kill(pid, signal.SIGTERM)
        end = time.time() + 3
        while pid_alive(pid) and time.time() < end:
            time.sleep(0.1)
        if pid_alive(pid):
            os.kill(pid, signal.SIGKILL)
            while pid_alive(pid):
                time.sleep(0.05)
        print(f"killed pid {pid}")
    else:
        print(f"pid {pid} was not running (stale state)")
    try:
        os.remove(STATE)
    except OSError:
        pass


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    name, args = sys.argv[1], sys.argv[2:]
    table = {
        "launch": lambda: cmd_launch(args),
        "info": lambda: print(json.dumps({k: window()[k] for k in ("address", "at", "size", "floating")})),
        "resize": lambda: cmd_resize(int(args[0]), int(args[1])),
        "tile": cmd_tile,
        "move": lambda: move(float(args[0]), float(args[1])),
        "click": lambda: cmd_click(float(args[0]), float(args[1]), *(args[2:3] or ["left"]), *(map(int, args[3:4]))),
        "drag": lambda: cmd_drag(*map(float, args[:4])),
        "scroll": lambda: cmd_scroll(float(args[0]), float(args[1]), int(args[2])),
        "key": lambda: cmd_key(*args),
        "shot": lambda: cmd_shot(args[0]),
        "alive": cmd_alive,
        "kill": cmd_kill,
    }
    table[name]()


main()
