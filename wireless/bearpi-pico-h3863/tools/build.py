#!/usr/bin/env python3
"""Build a WS63 (BearPi-Pico H3863) app via the SDK's out-of-tree flow.

Prepares the SDK (restores exec bits lost by git clone, verifies the
toolchain), then drives the SDK's build.py with FBB_* env vars. Artifacts
land in build/<app>/output/ws63/fwpkg/ws63-liteos-app/ (per-app isolated SDK
output root via FBB_BUILD_ROOT_PATH), so each app keeps its own cmake cache.

Usage (from the platform dir):
    python tools/build.py --app sle_decoy
    python tools/build.py -a sle_client
    python tools/build.py                 # default app
    python tools/build.py --clean         # clean then build
    python tools/build.py -a sle_decoy -c  # SDK args pass-through
"""
import argparse
import os
import subprocess
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PLATFORM_DIR = os.path.dirname(SCRIPT_DIR)
sys.path.insert(0, SCRIPT_DIR)

import patch_nv_coex  # noqa: E402


def prepare_sdk(sdk_dir):
    compiler_root = os.path.join(sdk_dir, "tools", "bin", "compiler", "riscv",
                                 "cc_riscv32_musl_105", "cc_riscv32_musl")
    if not os.path.isdir(compiler_root):
        print(f"error: toolchain not found at {compiler_root}", file=sys.stderr)
        sys.exit(1)

    # Restore exec bits lost by git clone.
    tools_bin = os.path.join(sdk_dir, "tools", "bin")
    if os.path.isdir(tools_bin):
        for root, _, files in os.walk(tools_bin):
            for name in files:
                path = os.path.join(root, name)
                if os.access(path, os.X_OK) == False:  # noqa: E712
                    try:
                        os.chmod(path, os.stat(path).st_mode | 0o111)
                    except OSError:
                        pass

    print(f"WS63 SDK prepared at {sdk_dir}")
    print(f"Toolchain: {compiler_root}")


def patch_coex(sdk_dir, target):
    """Set NV btc_channel_scan_switch=0 so coex stops masking adv channels."""
    try:
        print(f"[coex-patch] {patch_nv_coex.apply(sdk_dir)}")
    except Exception as exc:  # noqa: BLE001
        print(f"[coex-patch] WARNING: skipped ({exc})", file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(description="Build a WS63 app")
    parser.add_argument("--app", "-a", default="default", help="app name (default: default)")
    parser.add_argument("--clean", action="store_true", help="clean then build")
    parser.add_argument("--no-coex-patch", action="store_true",
                        help="skip the WS63 advertising coex-channel patch")
    parser.add_argument("args", nargs="*", help="extra args passed to the SDK build.py")
    args = parser.parse_args()

    sdk_dir = os.environ.get("FBB_SDK_DIR", os.path.expanduser("~/workspace/fbb_ws63/src"))
    if not os.path.isdir(sdk_dir):
        print(f"error: WS63 SDK not found at {sdk_dir} (set FBB_SDK_DIR)", file=sys.stderr)
        sys.exit(1)

    prepare_sdk(sdk_dir)

    target = "ws63-liteos-app"
    if not args.no_coex_patch:
        patch_coex(sdk_dir, target)
    build_root = os.environ.get("FBB_BUILD_ROOT_PATH",
                                os.path.join(PLATFORM_DIR, "build", args.app))
    os.makedirs(build_root, exist_ok=True)

    env = dict(os.environ)
    env["FBB_PROJECT_DIR"] = PLATFORM_DIR
    env["FBB_PROJECT_TARGET"] = target
    env["FBB_KCONFIG_CONFIG"] = os.environ.get("FBB_KCONFIG_CONFIG",
                                               os.path.join(PLATFORM_DIR, "build.config"))
    env["FBB_APP"] = args.app
    env["FBB_BUILD_ROOT_PATH"] = build_root

    sdk_args = [target]
    if args.clean:
        sdk_args.append("-c")
    sdk_args += args.args

    sys.exit(subprocess.run(["python3", "build.py"] + sdk_args,
                            env=env, cwd=sdk_dir).returncode)


if __name__ == "__main__":
    main()