#!/usr/bin/env python3
"""
kbpad - keyboard as a virtual Xbox 360 controller (Linux, uinput), to be player 4 in so4coop.

Steam sees the virtual controller as one more controller (Steam Input), so the mod treats it like
any other: steamN_position in so4coop.ini. The keyboard is grabbed while the script runs, so the
game doesn't receive the same keys twice; keys with no controller function (F1-F12, Esc, Tab,
Alt+Tab...) still work normally through a virtual keyboard.

Scroll Lock or Pause: turns controller mode on or off (off = normal keyboard).

Key map (player 4):
  W A S D            left stick (move)
  arrow keys         right stick (camera)
  J / K / U / I      A / B / X / Y   (Space is also A)
  Q / E              LB / RB
  Z / C              LT / RT (triggers; skills)
  Left Ctrl / Right Ctrl (or R)  L3 / R3 (R3 = tag-team; keyd turns Right Ctrl into Right Shift)
  T / G / F / H      D-pad up / down / left / right
  Enter / Backspace  Start / Back

Usage: kbpad.py [--device /dev/input/eventN] [--list]
Needs to read the keyboard (/dev/input/event*) and write to /dev/uinput.
"""
import argparse
import os
import select
import signal
import sys
import time

import evdev
from evdev import AbsInfo, InputDevice, UInput, ecodes as e

PREFERRED = "/dev/input/by-id/usb-Telink_Wireless_Receiver_TLSR8366-if01-event-kbd"
FULL = 32767

BUTTONS = {
    e.KEY_J: e.BTN_A, e.KEY_SPACE: e.BTN_A,
    e.KEY_K: e.BTN_B,
    e.KEY_U: e.BTN_X,
    e.KEY_I: e.BTN_Y,
    e.KEY_Q: e.BTN_TL, e.KEY_E: e.BTN_TR,
    e.KEY_LEFTCTRL: e.BTN_THUMBL, e.KEY_RIGHTSHIFT: e.BTN_THUMBR, e.KEY_R: e.BTN_THUMBR,
    e.KEY_ENTER: e.BTN_START, e.KEY_BACKSPACE: e.BTN_SELECT,
}
TRIGGERS = {e.KEY_Z: e.ABS_Z, e.KEY_C: e.ABS_RZ}
# axis -> (negative key, positive key)
STICKS = {
    e.ABS_X: (e.KEY_A, e.KEY_D), e.ABS_Y: (e.KEY_W, e.KEY_S),
    e.ABS_RX: (e.KEY_LEFT, e.KEY_RIGHT), e.ABS_RY: (e.KEY_UP, e.KEY_DOWN),
}
HATS = {e.ABS_HAT0X: (e.KEY_F, e.KEY_H), e.ABS_HAT0Y: (e.KEY_T, e.KEY_G)}
TOGGLE = {e.KEY_SCROLLLOCK, e.KEY_PAUSE}
PAD_KEYS = set(BUTTONS) | set(TRIGGERS) | {k for p in STICKS.values() for k in p} | {k for p in HATS.values() for k in p}


def log(msg):
    print(time.strftime("%H:%M:%S"), msg, flush=True)


def find_keyboard(path):
    if path:
        return InputDevice(path)
    # when keyd is running, it grabs the physical keyboard and the keys come out of its virtual keyboard
    for p in evdev.list_devices():
        try:
            d = InputDevice(p)
        except OSError:
            continue
        if d.name == "keyd virtual keyboard":
            return d
        d.close()
    if os.path.exists(PREFERRED):
        return InputDevice(PREFERRED)
    for p in evdev.list_devices():
        try:
            d = InputDevice(p)
        except OSError:
            continue
        keys = d.capabilities().get(e.EV_KEY, [])
        if e.KEY_A in keys and e.KEY_Z in keys and e.KEY_ENTER in keys and not d.name.startswith("so4kbpad"):
            return d
        d.close()
    sys.exit("kbpad: no keyboard found (or no permission to read /dev/input)")


def make_pad():
    stick = AbsInfo(value=0, min=-32768, max=32767, fuzz=16, flat=128, resolution=0)
    trig = AbsInfo(value=0, min=0, max=255, fuzz=0, flat=0, resolution=0)
    hat = AbsInfo(value=0, min=-1, max=1, fuzz=0, flat=0, resolution=0)
    caps = {
        e.EV_KEY: sorted(set(BUTTONS.values())),
        e.EV_ABS: [(e.ABS_X, stick), (e.ABS_Y, stick), (e.ABS_RX, stick), (e.ABS_RY, stick),
                   (e.ABS_Z, trig), (e.ABS_RZ, trig), (e.ABS_HAT0X, hat), (e.ABS_HAT0Y, hat)],
    }
    # VID/PID of a wired Xbox 360 controller: Steam and SDL recognize it without any setup
    return UInput(caps, name="Microsoft X-Box 360 pad", vendor=0x045E, product=0x028E, version=0x110, bustype=e.BUS_USB)


def main():
    ap = argparse.ArgumentParser(description="keyboard as a virtual Xbox 360 controller")
    ap.add_argument("--device", help="keyboard to use (/dev/input/eventN)")
    ap.add_argument("--list", action="store_true", help="list the input devices and exit")
    a = ap.parse_args()
    if a.list:
        for p in evdev.list_devices():
            print(p, InputDevice(p).name)
        return

    kbd = find_keyboard(a.device)
    pad = make_pad()
    # its own id (1209:5343): keyd must ignore it ([ids] -1209:5343), or it grabs the passthrough and the keys loop back
    vkbd = UInput.from_device(kbd, name="so4kbpad passthrough", vendor=0x1209, product=0x5343)
    held = set()
    pad_on = True

    def axis(neg, pos, scale):
        return (scale if pos in held else 0) - (scale if neg in held else 0)

    def sync_pad():
        for ax, (n, p) in STICKS.items():
            v = axis(n, p, FULL)
            # diagonal: same distance from the center as a straight direction
            other = {e.ABS_X: e.ABS_Y, e.ABS_Y: e.ABS_X, e.ABS_RX: e.ABS_RY, e.ABS_RY: e.ABS_RX}[ax]
            if v and axis(*STICKS[other], 1):
                v = int(v * 0.7071)
            pad.write(e.EV_ABS, ax, v)
        for ax, (n, p) in HATS.items():
            pad.write(e.EV_ABS, ax, axis(n, p, 1))
        for k, ax in TRIGGERS.items():
            pad.write(e.EV_ABS, ax, 255 if k in held else 0)
        for btn in set(BUTTONS.values()):
            pad.write(e.EV_KEY, btn, 1 if any(k in held for k, b in BUTTONS.items() if b == btn) else 0)
        pad.syn()

    def stop(*_):
        raise KeyboardInterrupt

    signal.signal(signal.SIGTERM, stop)
    kbd.grab()
    log(f"kbpad: keyboard '{kbd.name}' ({kbd.path}) -> virtual controller '{pad.name}' ({pad.device.path}); Scroll Lock/Pause turns it on/off")
    try:
        while True:
            select.select([kbd.fd], [], [])
            for ev in kbd.read():
                if ev.type == e.EV_KEY and ev.code in TOGGLE:
                    if ev.value == 1:
                        pad_on = not pad_on
                        held.clear()
                        sync_pad()
                        log("kbpad: controller mode " + ("ON" if pad_on else "off (normal keyboard)"))
                    continue
                if pad_on and ev.type == e.EV_KEY and ev.code in PAD_KEYS:
                    if ev.value == 1:
                        held.add(ev.code)
                    elif ev.value == 0:
                        held.discard(ev.code)
                    else:
                        continue                        # auto-repeat: nothing changes
                    sync_pad()
                    continue
                if ev.type in (e.EV_KEY, e.EV_SYN, e.EV_MSC):
                    vkbd.write(ev.type, ev.code, ev.value)
    except (KeyboardInterrupt, OSError) as x:
        if isinstance(x, OSError):
            log(f"kbpad: keyboard disappeared ({x})")
    finally:
        held.clear()
        try:
            sync_pad()
            kbd.ungrab()
        except OSError:
            pass
        pad.close()
        vkbd.close()
        log("kbpad: stopped")


if __name__ == "__main__":
    main()
