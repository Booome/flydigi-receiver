#!/usr/bin/env python3
"""ESP-IDF build wrapper for bluetooth/esp32-wroom-32e apps.

Usage:
    python3 tools/build.py            # default app = hello_world
    python3 tools/build.py --app bt_inquiry
    python3 tools/build.py --clean     # rm build/<app>/
    python3 tools/build.py --no-set-target  # skip set-target esp32

Activates /opt/esp-idf/export.sh, then runs idf.py with -C apps/<app>
and -B ../../build/<app> (so build output lands at project-level build/<app>).
"""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
APPS_DIR = PROJECT_ROOT / "apps"
BUILD_DIR = PROJECT_ROOT / "build"
IDF_PATH = Path("/opt/esp-idf")


def activate_idf() -> None:
    if shutil.which("idf.py") is not None:
        return
    export_sh = IDF_PATH / "export.sh"
    if not export_sh.exists():
        sys.exit(f"idf.py not on PATH and {export_sh} not found; "
                 "install esp-idf (yay -S esp-idf) or source its env manually")
    subprocess.run(["bash", "-c", f"source {export_sh} >/dev/null 2>&1 && "
                                   "which idf.py"],
                   check=True)


def app_dir(name: str) -> Path:
    p = APPS_DIR / name
    if not p.is_dir():
        sys.exit(f"app not found: {p} (existing apps: "
                 f"{', '.join(d.name for d in APPS_DIR.iterdir() if d.is_dir())})")
    return p


def run(cmd: list[str], cwd: Path) -> None:
    print(f"$ {' '.join(cmd)} (cwd={cwd})", flush=True)
    res = subprocess.run(cmd, cwd=cwd)
    if res.returncode != 0:
        sys.exit(res.returncode)


def main() -> None:
    ap = argparse.ArgumentParser(description="Build an ESP-IDF app under bluetooth/esp32-wroom-32e")
    ap.add_argument("--app", default="default", help="app name under apps/")
    ap.add_argument("--clean", action="store_true",
                    help="rm build/<app>/ before building")
    ap.add_argument("--no-set-target", action="store_true",
                    help="skip 'idf.py set-target esp32' (assumes already set)")
    args = ap.parse_args()

    activate_idf()

    app = app_dir(args.app)
    build = BUILD_DIR / args.app
    build_rel = os.path.relpath(build, app)
    # When running from apps/<app>/, "build" is at ../../build/<app>/

    if args.clean and build.exists():
        print(f"removing {build}", flush=True)
        shutil.rmtree(build)

    if not args.no_set_target:
        run(["idf.py", "-B", build_rel, "set-target", "esp32"], cwd=app)

    run(["idf.py", "-B", build_rel, "build"], cwd=app)


if __name__ == "__main__":
    main()