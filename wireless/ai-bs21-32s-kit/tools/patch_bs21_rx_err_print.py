#!/usr/bin/env python3
"""Diagnostic patch: make BS21 log SLE announce RX errors unconditionally.

evt_task_gle_rxpd_rx_error_func (evt_task_gle_acb_isr.c.obj in libbgtp.a) only
prints the compressed RX-error line when (rx_mic_en && err_type==1). Neutralize
the two skip-branches so ANY announce RX error prints, turning the BS21 UART
into an "announce frames received but rejected downstream" probe.

Original (RISC-V LE):
  beqz a0,.L62     01 cd     -> c.nop              01 00
  bnei s1,1,.L62   bb 95 04 01 -> addi a5,x0,0      93 07 00 00

Diagnostic bridge only - revert with --restore; not for shipping.
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile

MEMBER = "evt_task_gle_acb_isr.c.obj"
OLD = bytes.fromhex("01cdbb950401")
NEW = bytes.fromhex("010093070000")


def sdk_dir():
    return os.environ.get("BS21_SDK_DIR", os.path.expanduser("~/.local/Ai-BS21_SDK"))


def lib_path(root, target):
    return os.path.join(root, "protocol/bt/controller/bgtp", target, "libbgtp.a")


def _tool(root, name):
    p = os.path.join(root, "tools/bin/compiler/riscv/cc_riscv32_musl_b010",
                     "cc_riscv32_musl_fp/bin", f"riscv32-linux-musl-{name}")
    if not os.path.isfile(p):
        raise RuntimeError(f"{p} not found")
    os.chmod(p, 0o755)
    return p


def _run(cmd, cwd=None):
    r = subprocess.run(cmd, capture_output=True, text=True, cwd=cwd)
    if r.returncode != 0:
        raise RuntimeError(f"{' '.join(cmd)}\n{r.stderr}")
    return r.stdout


def apply(root, target="bs21-n1100-sle-central"):
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
        if data.count(NEW):
            return "already patched"
        n = data.count(OLD)
        if n != 1:
            raise RuntimeError(f"expected 1 context, found {n}; SDK changed?")
        open(obj, "wb").write(data.replace(OLD, NEW))
        _run([ar, "r", lib, obj], cwd=td)
    return f"patched {MEMBER} ({target}) to log RX errors unconditionally"


def restore(root, target="bs21-n1100-sle-central"):
    lib = lib_path(root, target)
    backup = lib + ".orig"
    if os.path.isfile(backup):
        shutil.copy2(backup, lib)
        return f"restored {lib}"
    return "nothing to restore"


def main():
    ap = argparse.ArgumentParser(description="BS21 announce RX-error log diagnostic")
    ap.add_argument("--target", default="bs21-n1100-sle-central")
    ap.add_argument("--restore", action="store_true")
    args = ap.parse_args()
    try:
        print(restore(sdk_dir(), args.target) if args.restore else apply(sdk_dir(), args.target))
    except RuntimeError as exc:
        sys.exit(f"error: {exc}")


if __name__ == "__main__":
    main()