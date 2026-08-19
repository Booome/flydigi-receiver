#!/usr/bin/env python3
"""BS21 board-to-board connect helper.

- Configures reset pins to open-drain (STM32 GPIO mode lost on reset).
- Connects G (sle_pair, board A) to T (sle_accept, board B).
- Order matters: reset G first (kills any stale A that grabs B's announce),
  then reset B so it re-broadcasts, then wait for A to seek+connect.
- Connection confirmed by B's `[Connected]` and A's `param update result`.
"""
import os, serial, time, subprocess, sys

CTRL = "/dev/serial/by-path/pci-0000:00:14.0-usb-0:3.4.1.1:1.0-port0"
APATH = "/dev/serial/by-path/pci-0000:00:14.0-usb-0:3.4.1.2:1.0-port0"
BPATH = "/dev/serial/by-path/pci-0000:00:14.0-usb-0:3.4.1.3:1.0-port0"
PIN_A = "8"
PIN_B = "11"


def pulse(pin, ms=2000):
    subprocess.run(["uart-gpio", "pulse", CTRL, "A", pin, "0", str(ms)],
                   check=True, capture_output=True)


def config_open_drain():
    for pin in (PIN_A, PIN_B):
        r = subprocess.run(["uart-gpio", "config", CTRL, "A", pin, "open-drain"],
                           capture_output=True, text=True)
        subprocess.run(["uart-gpio", "write", CTRL, "A", pin, "1"],
                       capture_output=True)
        print(f"  [{pin}] {r.stdout.strip()}", flush=True)


def open_ser(path, tries=5):
    for _ in range(tries):
        try:
            s = serial.Serial(path, 115200, timeout=0.1)
            s.reset_input_buffer()
            return s
        except Exception:
            time.sleep(0.3)
    return None


def connect_once(timeout=60):
    """Reset A first, then B; wait for A<->B connection."""
    sa = open_ser(APATH)
    sb = open_ser(BPATH)
    if sa is None or sb is None:
        return False
    pulse(PIN_A)          # kill stale A so it releases B
    time.sleep(1.5)
    pulse(PIN_B)          # B re-broadcasts
    t0 = time.time()
    ok = False
    while time.time() - t0 < timeout:
        try:
            ca = sa.read(8192)
            cb = sb.read(8192)
        except Exception:
            break
        if cb and b"announce enable" in cb:
            pass
        if ca and b"param update result" in ca:
            ok = True
            break
        if cb and b"Connected param update" in cb:
            ok = True
            break
        time.sleep(0.02)
    sa.close()
    sb.close()
    return ok


def main():
    config_open_drain()
    for name, p in (("board_a(G)", APATH), ("board_b(T)", BPATH)):
        if not os.path.exists(p):
            print(f"[ERROR] {name} not present at {p}; plug it in", flush=True)
            return 2
    rounds = int(sys.argv[1]) if len(sys.argv) > 1 else 5
    for i in range(1, rounds + 1):
        print(f"--- round {i} ---", flush=True)
        if connect_once():
            print("CONNECTED", flush=True)
            return 0
        print("  not connected, retry", flush=True)
    print("ALL FAILED", flush=True)
    return 1


if __name__ == "__main__":
    sys.exit(main())
