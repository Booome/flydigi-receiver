#!/usr/bin/env python3
"""capture_uart: capture board serial output with optional delayed reset.

Usage:
    python3 tools/capture_uart.py --board-a --board-b --rst-a --duration 60 --odir /tmp

Board/serial mapping is read from the project .env file:
    BOARD_A_PORT / BOARD_A_RST_PORT / BOARD_A_RST_PIN / BOARD_A_TYPE
    BOARD_B_PORT / BOARD_B_RST_PORT / BOARD_B_RST_PIN / BOARD_B_TYPE

BOARD_*_TYPE selects the reset mechanism when --rst-a/--rst-b is given:
    sle   (default) -> uart-gpio pulse via ctrl board pin (BS21/WS63)
    esp32           -> DTR toggle via open serial port (ESP32 DevKitC, with
                       onboard USB-UART bridge wiring DTR->EN)
"""

import argparse
import os
import select
import serial
import subprocess
import sys
import time
from datetime import datetime

BAUD = 115200
DEFAULT_RESET_DELAY = 1.0
DEFAULT_DURATION = 60
DEFAULT_ODIR = "/tmp"
RESET_PULSE_MS = 2000


def parse_args(argv=None):
    p = argparse.ArgumentParser(description="Capture BS21/ESP32 board serial output")
    p.add_argument("--board-a", action="store_true", help="capture board_a")
    p.add_argument("--board-b", action="store_true", help="capture board_b")
    p.add_argument("--rst-a", action="store_true", help="reset board_a after connect")
    p.add_argument("--rst-b", action="store_true", help="reset board_b after connect")
    p.add_argument("--reset-delay", type=float, default=DEFAULT_RESET_DELAY,
                   help="seconds to wait before reset (default: %(default)s)")
    p.add_argument("--duration", type=int, default=DEFAULT_DURATION,
                   help="capture duration in seconds (default: %(default)s)")
    p.add_argument("--odir", default=DEFAULT_ODIR, help="output directory (default: %(default)s)")
    p.add_argument("--ts", action="store_true", help="prefix each line with [+sec.msec]")
    p.add_argument("--no-echo", action="store_true", help="disable live terminal echo")
    args = p.parse_args(argv)
    if not (args.board_a or args.board_b):
        p.error("must select at least one board: --board-a and/or --board-b")
    if args.rst_a and not args.board_a:
        p.error("--rst-a requires --board-a")
    if args.rst_b and not args.board_b:
        p.error("--rst-b requires --board-b")
    return args
    return args


def make_output_path(odir, board, start_dt):
    return os.path.join(odir, "%s_%s.log" % (board, start_dt.strftime("%Y%m%d_%H%M%S")))


def ts_prefix(elapsed):
    s, ms = divmod(round(elapsed * 1000), 1000)
    return ("[+%d.%03d] " % (s, ms)).encode()


class LineTimestamping:
    """Split raw bytes into lines on \\n, tagging each with elapsed seconds."""

    def __init__(self, t0):
        self.t0 = t0
        self.buf = bytearray()

    def feed(self, data):
        self.buf.extend(data)
        out = []
        while True:
            idx = self.buf.find(b"\n")
            if idx < 0:
                break
            line = bytes(self.buf[: idx + 1])
            del self.buf[: idx + 1]
            out.append((time.monotonic() - self.t0, line))
        return out

    def flush(self):
        if not self.buf:
            return None
        tail = bytes(self.buf)
        self.buf.clear()
        return time.monotonic() - self.t0, tail


def capture(streams, outs, duration, ts, echo, reset_plan=None, delay=DEFAULT_RESET_DELAY):
    t0 = time.monotonic()
    stamps = {b: LineTimestamping(t0) for b in outs}
    totals = {b: 0 for b in outs}
    end = t0 + duration
    interrupted = False
    try:
        if reset_plan:
            time.sleep(max(0.0, delay))
            for fn in reset_plan:
                try:
                    fn()
                except Exception as e:
                    print("[ERROR] reset failed: %s" % e, file=sys.stderr)
        while time.monotonic() < end:
            r, _, _ = select.select(list(streams), [], [], 0.05)
            for s in r:
                board = streams[s]
                try:
                    data = s.read(4096)
                except OSError as e:
                    print("[WARN] %s read error: %s" % (board, e), file=sys.stderr)
                    streams.pop(s, None)
                    continue
                if not data:
                    streams.pop(s, None)
                    continue
                if echo:
                    sys.stdout.buffer.write(data)
                    sys.stdout.buffer.flush()
                if ts:
                    for elapsed, line in stamps[board].feed(data):
                        outs[board].write(ts_prefix(elapsed) + line)
                else:
                    outs[board].write(data)
                totals[board] += len(data)
    except KeyboardInterrupt:
        interrupted = True
    for b in outs:
        tail = stamps[b].flush()
        if ts and tail:
            outs[b].write(ts_prefix(tail[0]) + tail[1])
        outs[b].flush()
    return totals, interrupted


def load_env(env_path):
    env = {}
    with open(env_path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            k, v = line.split("=", 1)
            env[k.strip()] = v.strip()
    return env


def board_key(board):
    return board.split("_", 1)[-1].upper()


def pulse_reset_sle(board, env):
    """External uart-gpio reset for BS21/WS63 (reset pin wired to ctrl board)."""
    key = board_key(board)
    port = env.get("BOARD_%s_RST_PORT" % key)
    pin = env.get("BOARD_%s_RST_PIN" % key)
    if not (port and pin):
        raise RuntimeError("missing BOARD_%s_RST_PORT/PIN in .env" % key)
    subprocess.run(["uart-gpio", "config", port, "A", pin, "open-drain"],
                   check=True)
    subprocess.run(["uart-gpio", "pulse", port, "A", pin, "0", str(RESET_PULSE_MS)],
                   check=True)


def pulse_reset_esp32(stream):
    """DTR toggle for ESP32 DevKitC (DTR->EN wiring on the onboard USB-UART).

    Toggles DTR line — works regardless of DTR polarity (active-high vs
    active-low EN). ESP32 sees a reset pulse and boots normally.
    """
    stream.dtr = not stream.dtr
    time.sleep(0.1)
    stream.dtr = not stream.dtr
    time.sleep(0.5)


def reset_plan(args, env, streams):
    """Return list of zero-arg reset callables, one per requested board.

    Board type is read from .env (BOARD_A_TYPE / BOARD_B_TYPE, default 'sle'):
      - sle   -> uart-gpio pulse via ctrl board (BS21/WS63)
      - esp32 -> DTR toggle via the open serial stream (ESP32 DevKitC)
    """
    plan = []
    board_to_stream = {b: s for s, b in streams.items()}
    for board, flag in (("board_a", args.rst_a), ("board_b", args.rst_b)):
        if not flag:
            continue
        key = board_key(board)
        btype = env.get("BOARD_%s_TYPE" % key, "sle").lower()
        if btype not in ("sle", "esp32"):
            print("[WARN] unknown BOARD_%s_TYPE=%r, falling back to 'sle'" %
                  (key, btype), file=sys.stderr)
            btype = "sle"
        if btype == "esp32":
            stream = board_to_stream.get(board)
            if stream is None:
                raise RuntimeError("BOARD_%s_TYPE=esp32 but %s not open" %
                                   (key, board))
            plan.append(lambda s=stream: pulse_reset_esp32(s))
        else:
            plan.append(lambda b=board: pulse_reset_sle(b, env))
    return plan


def open_streams(ports):
    streams = {}
    for board, port in ports.items():
        try:
            s = serial.Serial(port, BAUD, timeout=0.05)
            s.reset_input_buffer()
            streams[s] = board
        except Exception as e:
            print("[WARN] %s open failed at %s: %s" % (board, port, e), file=sys.stderr)
    return streams


def project_root():
    # Walk up from __file__ until we find .git (or .env), so the script works
    # from any checkout (main, worktree, ...) without hard-coded depth.
    cur = os.path.dirname(os.path.abspath(__file__))
    while cur != os.path.dirname(cur):
        if os.path.isdir(os.path.join(cur, ".git")) or os.path.isfile(
                os.path.join(cur, ".env")):
            return cur
        cur = os.path.dirname(cur)
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main(argv=None, env=None):
    args = parse_args(argv)
    if env is None:
        env_path = os.path.join(project_root(), ".env")
        if not os.path.isfile(env_path):
            print("[ERROR] .env not found at project root; see .env.example")
            return 1
        env = load_env(env_path)
    boards = [b for b in ("board_a", "board_b") if getattr(args, b)]
    ports = {}
    for b in boards:
        key = board_key(b)
        port = env.get("BOARD_%s_PORT" % key)
        if not port:
            print("[ERROR] missing BOARD_%s_PORT in .env" % key)
            return 1
        ports[b] = port
    streams = open_streams(ports)
    if not streams:
        print("[ERROR] no board serial port opened")
        return 1
    os.makedirs(args.odir, exist_ok=True)
    start = datetime.now()
    outs = {}
    for s, b in streams.items():
        path = make_output_path(args.odir, b, start)
        outs[b] = open(path, "wb")
        print("[capture] %s -> %s" % (b, path))
    try:
        totals, interrupted = capture(streams, outs, args.duration, args.ts,
                                      not args.no_echo,
                                      reset_plan(args, env, streams),
                                      args.reset_delay)
    finally:
        for f in outs.values():
            f.close()
        for s in streams:
            s.close()
    for b, total in totals.items():
        print("[capture] %s: %d bytes in %ds" % (b, total, args.duration))
    return 130 if interrupted else 0


if __name__ == "__main__":
    sys.exit(main())
