#!/usr/bin/env python3
"""Diagnostic patch: force WS63 SLE advertise SED frequency field to BS2x value.

Reverse-engineered divergence (libbgtp.a evt_task_gle_adv_config_sed):
  WS63 writes SED+128 (frequency) = 0x1110 (loaded from g_em_freq_tbl_offset).
  BS2x writes SED+128 = 0xEA8 (hardcoded imm). The four-quadrant test forces
  F_WS63_adv != F_BS21_adv under a pure-frequency model, so this patch points
  WS63's announce at the BS2x value to test whether a frequency mismatch is why
  BS2x cannot hear WS63 broadcasts.

The patch swaps:
  lui a3,0        (b7 06 00 00)
  lhu a3,0(a3)    (83 d6 06 00)   -> load g_em_freq_tbl_offset (0x1110)
for:
  lui a3,1        (b7 06 01 00)
  addi a3,a3,-344 (93 86 68 ea)   -> 0xEA8, exactly what BS21 hardcodes.

Only the ADV SED config is changed; scan/freq-tbl data is untouched.
Diagnostic bridge only - revert with --restore; not for shipping.
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile

MEMBER = "evt_task_gle_adv.c.obj"
OLD = bytes.fromhex("b706000083d60600")
NEW = bytes.fromhex("b7060100938668ea")
TOOLCHAIN_REL = ("tools/bin/compiler/riscv/cc_riscv32_musl_105/"
                 "cc_riscv32_musl_fp/bin")


def default_sdk_dir():
    return os.environ.get("FBB_SDK_DIR", os.path.expanduser("~/workspace/fbb_ws63/src"))


def lib_path(root, target):
    return os.path.join(root, "protocol/bt/controller/bgtp", target, "libbgtp.a")


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


def apply(root, target="ws63-liteos-app"):
    """Force the announce SED frequency field to 0xEA8. Idempotent."""
    lib = lib_path(root, target)
    if not os.path.isfile(lib):
        raise RuntimeError(f"{lib} not found")
    backup = lib + ".orig"
    if not os.path.isfile(backup):
        shutil.copy2(lib, backup)
    ar = _tool(root, "ar")
    with tempfile.TemporaryDirectory() as td:
        _run([ar, "x", lib, MEMBER], cwd=td)
        obj = os.path.join(td, MEMBER)
        data = open(obj, "rb").read()
        n = data.count(OLD)
        if data.count(NEW):
            return "already patched (0xEA8 present)"
        if n != 1:
            raise RuntimeError(f"expected 1 OLD context, found {n}; SDK changed?")
        open(obj, "wb").write(data.replace(OLD, NEW))
        _run([ar, "r", lib, obj], cwd=td)
    return f"patched announce SED freq -> 0xEA8 ({n} context replaced)"


def restore(root, target="ws63-liteos-app"):
    lib = lib_path(root, target)
    backup = lib + ".orig"
    if os.path.isfile(backup):
        shutil.copy2(backup, lib)
        return f"restored {lib}"
    return "nothing to restore"


def main():
    ap = argparse.ArgumentParser(description="WS63 announce SED freq diagnostic patch")
    ap.add_argument("--target", default="ws63-liteos-app")
    ap.add_argument("--restore", action="store_true")
    args = ap.parse_args()
    root = default_sdk_dir()
    try:
        print(restore(root, args.target) if args.restore else apply(root, args.target))
    except RuntimeError as exc:
        sys.exit(f"error: {exc}")


if __name__ == "__main__":
    main()