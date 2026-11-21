#!/usr/bin/env python3
"""
kbpad - teclado como controle virtual de Xbox 360 (Linux, uinput), para ser o 4o jogador do so4coop.

A Steam ve o controle virtual como mais um controle (Steam Input), entao o mod o trata como
qualquer outro: steamN_position no so4coop.ini. O teclado fica "agarrado" (grab) enquanto o script
roda, para o jogo nao receber as mesmas teclas duas vezes; teclas sem funcao no controle (F1-F12,
Esc, Tab, Alt+Tab...) seguem normalmente por um teclado virtual.

Scroll Lock ou Pause: liga/desliga o modo controle (desligado = teclado normal).

Mapa (jogador 4):
  W A S D            analogico esquerdo (andar)
  setas              analogico direito (camera)
  J / K / U / I      A / B / X / Y   (Espaco tambem = A)
  Q / E              LB / RB
  Z / C              LT / RT (gatilhos; habilidades)
  Ctrl esq. / Ctrl dir. (ou R)  L3 / R3 (R3 = tag-team; o keyd faz o Ctrl dir. virar Shift dir.)
  T / G / F / H      direcional cima / baixo / esquerda / direita
  Enter / Backspace  Start / Back

Uso: kbpad.py [--device /dev/input/eventN] [--list]
Precisa ler o teclado (/dev/input/event*) e escrever em /dev/uinput.
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
# eixo -> (tecla negativa, tecla positiva)
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
    # com o keyd rodando, ele agarra o teclado fisico e as teclas saem pelo teclado virtual dele
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
    sys.exit("kbpad: nenhum teclado encontrado (ou sem permissao para ler /dev/input)")


def make_pad():
    stick = AbsInfo(value=0, min=-32768, max=32767, fuzz=16, flat=128, resolution=0)
    trig = AbsInfo(value=0, min=0, max=255, fuzz=0, flat=0, resolution=0)
    hat = AbsInfo(value=0, min=-1, max=1, fuzz=0, flat=0, resolution=0)
    caps = {
        e.EV_KEY: sorted(set(BUTTONS.values())),
        e.EV_ABS: [(e.ABS_X, stick), (e.ABS_Y, stick), (e.ABS_RX, stick), (e.ABS_RY, stick),
                   (e.ABS_Z, trig), (e.ABS_RZ, trig), (e.ABS_HAT0X, hat), (e.ABS_HAT0Y, hat)],
    }
    # VID/PID de um controle Xbox 360 com fio: a Steam e o SDL o reconhecem sem configuracao
    return UInput(caps, name="Microsoft X-Box 360 pad", vendor=0x045E, product=0x028E, version=0x110, bustype=e.BUS_USB)


def main():
    ap = argparse.ArgumentParser(description="teclado como controle virtual de Xbox 360")
    ap.add_argument("--device", help="teclado a usar (/dev/input/eventN)")
    ap.add_argument("--list", action="store_true", help="lista os dispositivos de entrada e sai")
    a = ap.parse_args()
    if a.list:
        for p in evdev.list_devices():
            print(p, InputDevice(p).name)
        return

    kbd = find_keyboard(a.device)
    pad = make_pad()
    # id proprio (1209:5343): o keyd precisa ignora-lo ([ids] -1209:5343), senao agarra o repasse e as teclas voltam em laco
    vkbd = UInput.from_device(kbd, name="so4kbpad passthrough", vendor=0x1209, product=0x5343)
    held = set()
    pad_on = True

    def axis(neg, pos, scale):
        return (scale if pos in held else 0) - (scale if neg in held else 0)

    def sync_pad():
        for ax, (n, p) in STICKS.items():
            v = axis(n, p, FULL)
            # diagonal: mesma distancia do centro que uma direcao reta
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
    log(f"kbpad: teclado '{kbd.name}' ({kbd.path}) -> controle virtual '{pad.name}' ({pad.device.path}); Scroll Lock/Pause liga/desliga")
    try:
        while True:
            select.select([kbd.fd], [], [])
            for ev in kbd.read():
                if ev.type == e.EV_KEY and ev.code in TOGGLE:
                    if ev.value == 1:
                        pad_on = not pad_on
                        held.clear()
                        sync_pad()
                        log("kbpad: modo controle " + ("LIGADO" if pad_on else "desligado (teclado normal)"))
                    continue
                if pad_on and ev.type == e.EV_KEY and ev.code in PAD_KEYS:
                    if ev.value == 1:
                        held.add(ev.code)
                    elif ev.value == 0:
                        held.discard(ev.code)
                    else:
                        continue                        # repeticao automatica: nada muda
                    sync_pad()
                    continue
                if ev.type in (e.EV_KEY, e.EV_SYN, e.EV_MSC):
                    vkbd.write(ev.type, ev.code, ev.value)
    except (KeyboardInterrupt, OSError) as x:
        if isinstance(x, OSError):
            log(f"kbpad: teclado sumiu ({x})")
    finally:
        held.clear()
        try:
            sync_pad()
            kbd.ungrab()
        except OSError:
            pass
        pad.close()
        vkbd.close()
        log("kbpad: encerrado")


if __name__ == "__main__":
    main()
