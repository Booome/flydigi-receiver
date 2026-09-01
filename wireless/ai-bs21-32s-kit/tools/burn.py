#!/usr/bin/env python3
"""BS21 auto-flasher: run ws63flash under a pty and drive the chip reset.

Usage:
    python3 wireless/ai-bs21-32s-kit/tools/burn.py board_a [fwpkg]
    python3 wireless/ai-bs21-32s-kit/tools/burn.py board_a -a <app>   # e.g. t_broadcaster

Board/serial mapping is read from the project .env file (wireless/ai-bs21-32s-kit/../../.env):
    BOARD_A_PORT / BOARD_A_RST_PORT / BOARD_A_RST_PIN
    BOARD_B_PORT / BOARD_B_RST_PORT / BOARD_B_RST_PIN
FwPkg defaults to build/<app>/bs21_all_in_one.fwpkg (app=default -> build/).

Flow (one ws63flash run decides the chip state):
  1. wait for "Waiting for device reset" (instant when run on a pty),
  2. wait 2s: auto download -> boot-loop state, wait for finish,
  3. else pulse reset, wait 1s: download -> normal state, wait for finish,
  4. else chip stuck: retry up to DEAD_MAX_ATTEMPTS ws63flash runs.
"""

import os
import pty
import select
import shlex
import signal
import subprocess
import sys
import time

FLASH_BAUD = 460800
WAIT_WAITING_TIMEOUT = 6.0
AUTO_DL_WAIT = 2.0
POST_RESET_WAIT = 1.0
DONE_TIMEOUT = 240
DONE_MARKER = b"Done."
DONE_GRACE = 10.0
DEAD_MAX_ATTEMPTS = 5
DEAD_RETRY_SLEEP = 5

DOWNLOAD_MARKERS = (b"Xfer ", b"Establishing ymodem")


def repo_root():
    """Build output root (wireless/ai-bs21-32s-kit), where build/<app>/ fwpkg live."""
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def project_root():
    """Repository root (contains .env and wireless/)."""
    return os.path.dirname(os.path.dirname(repo_root()))


def load_env():
    """Load .env from project root."""
    env_path = os.path.join(project_root(), ".env")
    if not os.path.isfile(env_path):
        sys.exit("[ERROR] .env not found at project root; see .env.example")
    with open(env_path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            if "=" in line:
                k, v = line.split("=", 1)
                os.environ.setdefault(k.strip(), v.strip())
    return dict(os.environ)


ENV = load_env()


def get_board(env, name):
    """Return (module_port, reset_cmd) for board_a / board_b from .env vars."""
    if name == "board_a":
        port = env.get("BOARD_A_PORT")
        rst_port = env.get("BOARD_A_RST_PORT")
        pin = env.get("BOARD_A_RST_PIN")
    elif name == "board_b":
        port = env.get("BOARD_B_PORT")
        rst_port = env.get("BOARD_B_RST_PORT")
        pin = env.get("BOARD_B_RST_PIN")
    else:
        sys.exit(f"[ERROR] unknown board: {name} (board_a / board_b only)")
    if not (port and rst_port and pin):
        sys.exit(f"[ERROR] incomplete .env config for {name} "
                 f"(need BOARD_{name.upper()}_PORT/RST_PORT/RST_PIN)")
    reset_cmd = f"uart-gpio config {rst_port} A {pin} open-drain && " \
                f"uart-gpio pulse {rst_port} A {pin} 0 2000"
    return port, reset_cmd


def get_port_users(port):
    pids = set()
    for cmd in (["lsof", "-t", port], ["fuser", port]):
        try:
            r = subprocess.run(cmd, capture_output=True, text=True, timeout=10)
        except (FileNotFoundError, subprocess.TimeoutExpired):
            continue
        for tok in r.stdout.split():
            if tok.strip().isdigit():
                pids.add(int(tok))
    return sorted(pids)


def ensure_port_free(port):
    pids = get_port_users(port)
    if not pids:
        print(f"[port-check] {port} free")
        return
    print(f"[port-check] {port} in use by: {pids}")
    failed = []
    for pid in pids:
        try:
            os.kill(pid, signal.SIGKILL)
            print(f"  killed pid={pid}")
        except ProcessLookupError:
            pass
        except PermissionError:
            failed.append(pid)
    if failed:
        print("[ERROR] cannot kill port-holding processes (permission denied), run manually:")
        print(f"    sudo kill -9 {' '.join(map(str, failed))}")
        sys.exit(1)
    time.sleep(1)
    print("[port-check] port holders cleared")


def pulse_reset(reset_cmd):
    """reset_cmd may chain a config step via '&&' — run each part in order."""
    print(f"[reset] {reset_cmd}")
    for part in reset_cmd.split("&&"):
        subprocess.run(shlex.split(part.strip()), check=False)


def ws63flash_cmd(port, fwpkg):
    return ["ws63flash", "--flash", port, fwpkg, f"-b{FLASH_BAUD}"]


def pty_spawn(cmd):
    master, slave = pty.openpty()
    p = subprocess.Popen(cmd, stdout=slave, stderr=slave, close_fds=True)
    os.close(slave)
    return master, p


def pty_print(data):
    sys.stdout.write(data.decode("utf-8", "replace"))
    sys.stdout.flush()


def pty_read(buf, master, p, duration):
    end = time.time() + duration
    while time.time() < end and p.poll() is None:
        r, _, _ = select.select([master], [], [], 0.05)
        if r:
            try:
                data = os.read(master, 4096)
            except OSError:
                break
            if not data:
                break
            buf.extend(data)
            pty_print(data)


def wait_for_marker(buf, master, p, marker, timeout):
    end = time.time() + timeout
    while time.time() < end and p.poll() is None:
        if marker in buf:
            return True
        r, _, _ = select.select([master], [], [], 0.05)
        if r:
            try:
                data = os.read(master, 4096)
            except OSError:
                break
            if not data:
                break
            buf.extend(data)
            pty_print(data)
    return marker in buf


def has_download(buf):
    return any(m in buf for m in DOWNLOAD_MARKERS)


def wait_finish(master, p):
    """Wait for download to complete. Success once ws63flash reports 'Done'
    (all files transferred) even if the process then hangs while resetting
    the chip, or once it exits cleanly."""
    done = False
    end = time.time() + DONE_TIMEOUT
    while time.time() < end and p.poll() is None:
        r, _, _ = select.select([master], [], [], 0.1)
        if r:
            try:
                data = os.read(master, 4096)
            except OSError:
                break
            if not data:
                break
            pty_print(data)
            if DONE_MARKER in data:
                done = True
                break
    if done:
        grace_end = time.time() + DONE_GRACE
        while time.time() < grace_end and p.poll() is None:
            r, _, _ = select.select([master], [], [], 0.1)
            if r:
                try:
                    data = os.read(master, 4096)
                except OSError:
                    break
                if not data:
                    break
                pty_print(data)
        return True
    if p.poll() is None:
        p.kill()
        p.wait()
        print("[flash] download wait timed out, force killed")
        return False
    return p.returncode == 0


def flash_round(master, p, reset_cmd, buf):
    if not wait_for_marker(buf, master, p, b"Waiting for device reset",
                           WAIT_WAITING_TIMEOUT):
        print("[flash] ws63flash never entered wait state")
        return False
    print("[flash] ws63flash waiting, watching for auto download...")
    pty_read(buf, master, p, AUTO_DL_WAIT)
    if has_download(buf):
        print("[flash] auto download started (boot-loop state)")
        return wait_finish(master, p)
    print("[flash] no auto download, pulsing reset...")
    pulse_reset(reset_cmd)
    pty_read(buf, master, p, POST_RESET_WAIT)
    if has_download(buf):
        print("[flash] download started after reset (normal state)")
        return wait_finish(master, p)
    print("[flash] still no download after reset (chip stuck)")
    return False


def flash_auto(port, fwpkg, reset_cmd):
    for attempt in range(1, DEAD_MAX_ATTEMPTS + 1):
        print(f"[flash] === attempt {attempt}/{DEAD_MAX_ATTEMPTS} ws63flash ===")
        master, p = pty_spawn(ws63flash_cmd(port, fwpkg))
        buf = bytearray()
        ok = flash_round(master, p, reset_cmd, buf)
        if p.poll() is None:
            p.kill()
            p.wait()
        try:
            os.close(master)
        except OSError:
            pass
        if ok:
            return True
        if attempt < DEAD_MAX_ATTEMPTS:
            print(f"[flash] attempt failed, retrying in {DEAD_RETRY_SLEEP}s...")
            time.sleep(DEAD_RETRY_SLEEP)
    return False


def default_fwpkg(app="default"):
    if app == "default":
        return os.path.join(repo_root(), "build", "bs21_all_in_one.fwpkg")
    return os.path.join(repo_root(), "build", app, "bs21_all_in_one.fwpkg")


def main():
    sys.stdout.reconfigure(write_through=True)
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        sys.exit(1)
    board = args[0]
    app = "default"
    fwpkg = None
    i = 1
    while i < len(args):
        if args[i] in ("-a", "--app"):
            i += 1
            if i >= len(args):
                sys.exit("[ERROR] --app requires an app name")
            app = args[i]
        else:
            fwpkg = args[i]
        i += 1
    fwpkg = os.path.abspath(fwpkg if fwpkg else default_fwpkg(app))
    if not os.path.isfile(fwpkg):
        print(f"[ERROR] fwpkg not found: {fwpkg}")
        sys.exit(1)

    port, reset_cmd = get_board(ENV, board)
    print(f"[board] {board} ({port})")
    print(f"[firmware] {fwpkg}")
    print(f"[config] {os.path.join(project_root(), '.env')}")

    ensure_port_free(port)

    ok = flash_auto(port, fwpkg, reset_cmd)
    if ok:
        print(f"[flash] {board} flashed successfully!")
        sys.exit(0)

    print("[flash] FAILED: still no success after all attempts")
    print("[hint] unplug/replug the module USB power, then rerun.")
    sys.exit(1)


if __name__ == "__main__":
    main()