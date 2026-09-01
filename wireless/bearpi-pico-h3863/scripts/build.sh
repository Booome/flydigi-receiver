#!/usr/bin/env bash
# Build the WS63 (BearPi-Pico H3863) firmware via the SDK's out-of-tree
# project flow.
#
# The SDK's out-of-tree build is driven by the SDK's build.py (same path the
# `fbb build` CLI and the SDK sample project use). build.py derives the full
# set of -D parameters (RAM_COMPONENT, DEFINES, ccflags, toolchain, ...) from
# the ws63 target config, flips cmake's source dir to this project via
# FBB_PROJECT_DIR, and packages the final .fwpkg.
#
# Artifacts land in $FBB_SDK_DIR/output/ws63/fwpkg/ws63-liteos-app/ (the SDK
# tree is read-only w.r.t. source; output/ is gitignored).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
FBB_SDK_DIR="${FBB_SDK_DIR:-$HOME/workspace/fbb_ws63/src}"
FBB_KCONFIG_CONFIG="${FBB_KCONFIG_CONFIG:-$PROJECT_DIR/build.config}"
FBB_PROJECT_TARGET="${FBB_PROJECT_TARGET:-ws63-liteos-app}"
FBB_APP="${FBB_APP:-default}"

if [ ! -d "$FBB_SDK_DIR" ]; then
    echo "error: WS63 SDK not found at $FBB_SDK_DIR (set FBB_SDK_DIR)" >&2
    exit 1
fi

export FBB_PROJECT_DIR="$PROJECT_DIR"
export FBB_PROJECT_TARGET
export FBB_KCONFIG_CONFIG
export FBB_APP

cd "$FBB_SDK_DIR"

python3 build.py "$FBB_PROJECT_TARGET" "$@"

echo
echo "=== Build artifacts ==="
find "$FBB_SDK_DIR/output/ws63/fwpkg/$FBB_PROJECT_TARGET" -maxdepth 1 -type f -name "*.fwpkg" 2>/dev/null | sort
