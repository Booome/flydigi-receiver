#!/usr/bin/env bash
# Prepare the external Ai-BS21_SDK for building. These are environment-level
# fixes (not source changes): git clone drops the +x bits, and the rcu
# (SLE-only) target ships only libc/libm prebuilt while reusing the rest from
# standard-bs21-n1100.
set -euo pipefail

SDK="${AI_BS21_SDK_PATH:-$HOME/.local/Ai-BS21_SDK}"

if [ ! -d "$SDK" ]; then
    echo "error: SDK not found at $SDK (set AI_BS21_SDK_PATH)" >&2
    exit 1
fi

# 1. Restore exec bits lost by git clone (toolchain + sign_tool).
find "$SDK/tools/bin" -type f -exec chmod +x {} \; 2>/dev/null || true

# 2. Symlink missing LiteOS prebuilt libs for bs21-n1100-rcu.
LITEOS_DIR="$SDK/kernel/liteos/liteos_v208.6.0_b017"
for f in "$LITEOS_DIR"/standard-bs21-n1100/*.a; do
    name="$(basename "$f")"
    if [ ! -e "$LITEOS_DIR/bs21-n1100-rcu/$name" ]; then
        ln -s "../standard-bs21-n1100/$name" "$LITEOS_DIR/bs21-n1100-rcu/$name"
    fi
done

echo "SDK prepared at $SDK"
