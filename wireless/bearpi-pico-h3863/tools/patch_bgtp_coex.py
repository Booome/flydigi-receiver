#!/usr/bin/env python3
"""Clear bit2 of g_macro_cfg_flags in the WS63 BTC controller rom-data lib.

Background (reverse-engineered from libbgtp.a):
  lm_chnl_scan_get_gle_chnl_map() ANDs the host advertising channel_map with
  dm_co_get_air_used_chnl_map() only when (g_macro_cfg_flags & 0x04) != 0.
  g_macro_cfg_flags defaults to 0x800000d4 (bit2 set) in
  protocol/bt/controller/bgtp/<target>/libbgtp_rom_data.a, so WS63 advertising
  gets masked down by the WiFi/BT coexistence channel map -> single-channel
  announce, which the BS2x-family dongle cannot discover.

This patch clears bit2 (0x800000d4 -> 0x800000d0) so the advertise map honours
the full host channel_map. It is idempotent, keeps a one-time .orig backup, and
supports --restore to go back to stock.

build.py imports apply()/restore(); run this file directly for manual control.
"""

import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile

SECTION = ".data.g_macro_cfg_flags"
MEMBER = "btc_rom_data.c.obj"
MASK_BIT2 = ~0x04 & 0xFFFFFFFF
TOOLCHAIN_REL = ("tools/bin/compiler/riscv/cc_riscv32_musl_105/"
                 "cc_riscv32_musl_fp/bin")


def default_sdk_dir():
    return os.environ.get("FBB_SDK_DIR", os.path.expanduser("~/workspace/fbb_ws63/src"))


def lib_path(root, target):
    return os.path.join(root, "protocol/bt/controller/bgtp", target,
                        "libbgtp_rom_data.a")


def _tool(root, name):
    p = os.path.join(root, TOOLCHAIN_REL, f"riscv32-linux-musl-{name}")
    if not os.path.isfile(p):
        raise RuntimeError(f"{p} not found (toolchain not set up?)")
    os.chmod(p, 0o755)
    return p


def _run(cmd, cwd=None):
    r = subprocess.run(cmd, capture_output=True, text=True, cwd=cwd)
    if r.returncode != 0:
        raise RuntimeError(f"{' '.join(cmd)}\n{r.stderr}")
    return r.stdout


def _read_section(objdump, obj):
    out = _run([objdump, "-s", "-j", SECTION, obj])
    for line in out.splitlines():
        parts = line.split()
        # content lines look like: "0000 d4000080  ...."
        if len(parts) >= 2 and all(c in "0123456789abcdef" for c in parts[0]) \
                and len(parts[1]) == 8:
            return struct.unpack("<I", bytes.fromhex(parts[1]))[0]
    raise RuntimeError(f"{SECTION} not found in {obj}")


def apply(root, target="ws63-liteos-app", dry_run=False):
    """Clear bit2 of g_macro_cfg_flags. Idempotent. Returns a status string."""
    lib = lib_path(root, target)
    if not os.path.isfile(lib):
        raise RuntimeError(f"{lib} not found")
    backup = lib + ".orig"

    if not os.path.isfile(backup):
        shutil.copy2(lib, backup)

    objcopy = _tool(root, "objcopy")
    objdump = _tool(root, "objdump")
    ar = _tool(root, "ar")

    with tempfile.TemporaryDirectory() as td:
        _run([ar, "x", lib, MEMBER], cwd=td)
        obj = os.path.join(td, MEMBER)
        if not os.path.isfile(obj):
            raise RuntimeError("failed to extract archive member")
        cur = _read_section(objdump, obj)
        new = cur & MASK_BIT2
        if cur == new:
            return f"already patched (0x{cur:08x})"
        if dry_run:
            return f"would patch 0x{cur:08x} -> 0x{new:08x}"
        binfile = os.path.join(td, "flag.bin")
        with open(binfile, "wb") as fh:
            fh.write(struct.pack("<I", new))
        _run([objcopy, f"--update-section={SECTION}={binfile}", obj])
        _run([ar, "r", lib, obj], cwd=td)
    return f"patched 0x{cur:08x} -> 0x{new:08x}"


def restore(root, target="ws63-liteos-app"):
    """Restore the stock .a from the .orig backup."""
    lib = lib_path(root, target)
    backup = lib + ".orig"
    if os.path.isfile(backup):
        shutil.copy2(backup, lib)
        return f"restored {lib} from {backup}"
    return "nothing to restore (no .orig backup present)"


def main():
    ap = argparse.ArgumentParser(description="WS63 advertising coex-channel patch")
    ap.add_argument("--target", default="ws63-liteos-app")
    ap.add_argument("--restore", action="store_true", help="restore stock .a")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    root = default_sdk_dir()
    try:
        msg = restore(root, args.target) if args.restore else apply(root, args.target, args.dry_run)
    except RuntimeError as exc:
        sys.exit(f"error: {exc}")
    print(msg)


if __name__ == "__main__":
    main()
