#!/usr/bin/env bash
set -euo pipefail

FBB_SDK_DIR="${FBB_SDK_DIR:-$HOME/workspace/fbb_ws63/src}"

if [ ! -d "$FBB_SDK_DIR" ]; then
    echo "error: WS63 SDK not found at $FBB_SDK_DIR (set FBB_SDK_DIR)" >&2
    exit 1
fi

COMPILER_ROOT="$FBB_SDK_DIR/../tools/bin/compiler/riscv/cc_riscv32_musl_105/cc_riscv32_musl"
if [ ! -d "$COMPILER_ROOT" ]; then
    echo "error: toolchain not found at $COMPILER_ROOT" >&2
    exit 1
fi

# 恢复 git clone 丢失的 exec 位
find "$FBB_SDK_DIR/../tools/bin" -type f -exec chmod +x {} \; 2>/dev/null || true

echo "WS63 SDK prepared at $FBB_SDK_DIR"
echo "Toolchain: $COMPILER_ROOT"
