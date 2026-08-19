#!/usr/bin/env python3
"""BS21 bidirectional link-loss detection test.

Both directions:
  1. Configure reset pins to open-drain (STM32 GPIO mode lost on reset).
  2. Establish a stable G<->T connection (confirmed by param update).
  3. Ask the user to unplug the peer board; measure when the other side
     senses the link loss (supervision timeout, disc:0x7).

Usage:
  python3 bs21_disconnect_test.py          # run both directions
  python3 bs21_disconnect_test.py --dir t2g   # only T-down -> G-senses
  python3 bs21_disconnect_test.py --dir g2t   # only G-down -> T-senses
"""
import argparse, os, serial, subprocess, sys, time

CTRL = "/dev/serial/by-path/pci-0000:00:14.0-usb-0:3.4.1.1:1.0-port0"
APATH = "/dev/serial/by-path/pci-0000:00:14.0-usb-0:3.4.1.2:1.0-port0"
BPATH = "/dev/serial/by-path/pci-0000:00:14.0-usb-0:3.4.1.3:1.0-port0"
PIN_A = "8"
PIN_B = "11"
ALARM = "/usr/share/sounds/freedesktop/stereo/alarm-clock-elapsed.oga"


def pulse(pin, ms=2000):
    subprocess.run(["uart-gpio", "pulse", CTRL, "A", pin, "0", str(ms)],
                   check=True, capture_output=True)


def config_open_drain():
    for pin in (PIN_A, PIN_B):
        subprocess.run(["uart-gpio", "config", CTRL, "A", pin, "open-drain"],
                       capture_output=True)
        subprocess.run(["uart-gpio", "write", CTRL, "A", pin, "1"],
                       capture_output=True)


def open_ser(path, tries=5):
    for _ in range(tries):
        try:
            s = serial.Serial(path, 115200, timeout=0.1)
            s.reset_input_buffer()
            return s
        except Exception:
            time.sleep(0.3)
    return None


def connect(timeout=60):
    sa = open_ser(APATH)
    sb = open_ser(BPATH)
    if sa is None or sb is None:
        return False
    pulse(PIN_A)
    time.sleep(1.5)
    pulse(PIN_B)
    t0 = time.time()
    ok = False
    while time.time() - t0 < timeout:
        for s, want in ((sa, b"param update result"), (sb, b"Connected param update")):
            try:
                c = s.read(8192)
            except Exception:
                continue
            if c and want in c:
                ok = True
                break
        if ok:
            break
        time.sleep(0.02)
    sa.close()
    sb.close()
    return ok


def wait_device(path, present, timeout=40):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if os.path.exists(path) == present:
            return True
        time.sleep(0.5)
    return False


def beep():
    if os.path.exists(ALARM):
        subprocess.Popen(["paplay", ALARM])


def measure(observer_path, gone_path, gone_name, observer_name):
    """Unplug `gone`, watch `observer` for Disconnected. Returns (t_gone, t_disc, gap)."""
    so = open_ser(observer_path)
    if so is None:
        return None, None, None
    beep()
    print(f">>> 请拔掉 {gone_name} 的 USB（open-drain 释放态）！", flush=True)
    t0 = time.time()
    gone_at = None
    disc_at = None
    while time.time() - t0 < 45:
        if gone_at is None and not os.path.exists(gone_path):
            gone_at = time.time() - t0
            print(f"[1] {gone_name} USB 消失 t+{gone_at:.1f}s", flush=True)
        try:
            c = so.read(8192)
        except Exception:
            break
        if c:
            for ln in c.split(b"\n"):
                st = ln.decode(errors="replace").strip()
                if st and ("Disconnected" in st or "disc:0x" in st) and "rescan" not in st:
                    dt = time.time() - t0
                    print(f"{observer_name} t+{dt:.2f}s: {st[:80]}", flush=True)
                    if "Disconnected" in st and disc_at is None:
                        disc_at = dt
    so.close()
    return gone_at, disc_at, (disc_at - gone_at) if (disc_at and gone_at) else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", choices=["t2g", "g2t", "both"], default="both")
    args = ap.parse_args()

    for name, p in (("board_a(G)", APATH), ("board_b(T)", BPATH)):
        if not os.path.exists(p):
            print(f"[ERROR] {name} not present at {p}; plug it in", flush=True)
            return 2

    config_open_drain()

    results = {}
    dirs = ["t2g", "g2t"] if args.dir == "both" else [args.dir]
    for d in dirs:
        print(f"\n=== direction {d}: {('T down -> G senses' if d=='t2g' else 'G down -> T senses')} ===", flush=True)
        if not connect():
            print("[ERROR] could not establish connection, aborting", flush=True)
            return 1
        print("  connection stable, unplug target board", flush=True)
        if d == "t2g":
            gone_at, disc_at, gap = measure(APATH, BPATH, "board_b(T)", "G")
        else:
            gone_at, disc_at, gap = measure(BPATH, APATH, "board_a(G)", "T")
        ok = gap is not None and gap < 8
        results[d] = (gone_at, disc_at, gap)
        print(f"  {d}: {'PASS' if ok else 'FAIL'} (unplugged t+{gone_at:.1f}, sensed t+{disc_at}, gap {gap:.1f}s)" if gap else f"  {d}: FAIL (not sensed)")
        if d == "t2g" and gap is None:
            print("[HINT] ensure G re-announces after T is unplugged, then reconnect", flush=True)
        # ask to re-plug
        beep()
        target = BPATH if d == "t2g" else APATH
        print(f">>> 请插回 {('board_b(T)' if d=='t2g' else 'board_a(G)')}（自动等待，无需回车）...", flush=True)
        wait_device(target, True)

    print("\n=== SUMMARY ===")
    for d, (gone_at, disc_at, gap) in results.items():
        name = "T down -> G senses" if d == "t2g" else "G down -> T senses"
        print(f"{name}: {f'PASS, gap {gap:.1f}s' if gap is not None else 'FAIL'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
