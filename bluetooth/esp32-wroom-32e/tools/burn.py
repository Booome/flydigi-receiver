#!/usr/bin/env python3
"""ESP-IDF flash wrapper for bluetooth/esp32-wroom-32e apps.

Usage:
    python3 tools/burn.py                       # default: hello_world, --board-a
    python3 tools/burn.py --board-b             # use BOARD_B_PORT
    python3 tools/burn.py --app bt_inquiry
    python3 tools/burn.py --port /dev/ttyUSB0   # explicit override (skips .env)

Reads serial port from top-level .env (BOARD_A_PORT or BOARD_B_PORT).
ESP32 reuses the same keys as SLE — no new .env entries required.
"""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
REPO_ROOT = PROJECT_ROOT.parent.parent
ENV_FILE = REPO_ROOT / ".env"
APPS_DIR = PROJECT_ROOT / "apps"
BUILD_DIR = PROJECT_ROOT / "build"
IDF_PATH = Path("/opt/esp-idf")


def load_env() -> dict[str, str]:
    env: dict[str, str] = {}
    if ENV_FILE.is_file():
        for line in ENV_FILE.read_text().splitlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            if "=" in line:
                k, v = line.split("=", 1)
                env[k.strip()] = v.strip().strip('"').strip("'")
    return env


def activate_idf() -> None:
    if shutil.which("idf.py") is not None:
        return
    export_sh = IDF_PATH / "export.sh"
    if not export_sh.exists():
        sys.exit("idf.py not on PATH; install esp-idf (yay -S esp-idf) or "
                 "source /opt/esp-idf/export.sh first")
    subprocess.run(["bash", "-c", f"source {export_sh} >/dev/null 2>&1"],
                   check=True)


def app_dir(name: str) -> Path:
    p = APPS_DIR / name
    if not p.is_dir():
        sys.exit(f"app not found: {p}")
    return p


def run(cmd: list[str], cwd: Path) -> None:
    print(f"$ {' '.join(cmd)} (cwd={cwd})", flush=True)
    res = subprocess.run(cmd, cwd=cwd)
    if res.returncode != 0:
        sys.exit(res.returncode)


def main() -> None:
    ap = argparse.ArgumentParser(description="Flash an ESP-IDF app under bluetooth/esp32-wroom-32e")
    ap.add_argument("--app", default="default")
    ap.add_argument("--board-a", action="store_true",
                    help="use BOARD_A_PORT from .env")
    ap.add_argument("--board-b", action="store_true",
                    help="use BOARD_B_PORT from .env")
    ap.add_argument("--port", help="explicit serial port override")
    args = ap.parse_args()

    env = load_env()

    if args.board_a and args.board_b:
        ap.error("--board-a and --board-b are mutually exclusive; pick one")
    if not (args.board_a or args.board_b):
        ap.error("must select one board: --board-a or --board-b")

    if args.port:
        port = args.port
    else:
        key = "BOARD_B_PORT" if args.board_b else "BOARD_A_PORT"
        if key not in env:
            sys.exit(f"{key} not set in {ENV_FILE}; "
                     "either add it or pass --port <path>")
        port = env[key]

    activate_idf()

    app = app_dir(args.app)
    build = BUILD_DIR / args.app
    build_rel = os.path.relpath(build, app)

    run(["idf.py", "-B", build_rel, "-p", port, "flash"], cwd=app)


if __name__ == "__main__":
    main()