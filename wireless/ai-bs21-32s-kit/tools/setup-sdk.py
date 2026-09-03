#!/usr/bin/env python3
"""Prepare the external Ai-BS21_SDK for building.

These are environment-level fixes (not source changes): git clone drops the
+x bits on the toolchain, and the rcu (SLE-only) target ships only libc/libm
prebuilt while reusing the rest from standard-bs21-n1100.
"""
import os
import sys

SDK = os.environ.get("AI_BS21_SDK_PATH", os.path.expanduser("~/.local/Ai-BS21_SDK"))

if not os.path.isdir(SDK):
    print(f"error: SDK not found at {SDK} (set AI_BS21_SDK_PATH)", file=sys.stderr)
    sys.exit(1)

# 1. Restore exec bits lost by git clone (toolchain + sign_tool).
tools_bin = os.path.join(SDK, "tools", "bin")
if os.path.isdir(tools_bin):
    for root, _, files in os.walk(tools_bin):
        for name in files:
            path = os.path.join(root, name)
            if not os.access(path, os.X_OK):
                try:
                    os.chmod(path, os.stat(path).st_mode | 0o111)
                except OSError:
                    pass

# 2. Symlink missing LiteOS prebuilt libs for bs21-n1100-rcu.
liteos_dir = os.path.join(SDK, "kernel", "liteos", "liteos_v208.6.0_b017")
src_dir = os.path.join(liteos_dir, "standard-bs21-n1100")
dst_dir = os.path.join(liteos_dir, "bs21-n1100-rcu")
if os.path.isdir(src_dir) and os.path.isdir(dst_dir):
    for name in os.listdir(src_dir):
        if not name.endswith(".a"):
            continue
        dst = os.path.join(dst_dir, name)
        if not os.path.exists(dst):
            try:
                os.symlink(f"../standard-bs21-n1100/{name}", dst)
            except OSError:
                pass

print(f"SDK prepared at {SDK}")