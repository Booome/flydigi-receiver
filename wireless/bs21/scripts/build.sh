#!/usr/bin/env bash
# Build the Flydigi BS21 SLE firmware using the external Ai-BS21_SDK.
# The SDK is only referenced, never modified in place for our changes:
# our sources live in overlay/ and are applied at build time.
set -euo pipefail

SDK="${AI_BS21_SDK_PATH:-$HOME/.local/Ai-BS21_SDK}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OVERLAY="$ROOT/overlay"
OUTPUT="$ROOT/output"

PYTHON="${PYTHON:-python3}"
TARGET="bs21-n1100-rcu"
STD_DIR="$SDK/application/bs21/standard"

if [ ! -d "$SDK" ]; then
    echo "error: SDK not found at $SDK (set AI_BS21_SDK_PATH)" >&2
    exit 1
fi

# 0. Prepare SDK (exec bits + LiteOS lib symlinks; idempotent env prep).
"$ROOT/scripts/setup-sdk.sh"

# Cleanup: revert our build-time overlay so the SDK tree stays pristine.
cleanup() {
    rm -f "$STD_DIR/ble_stub.c"
    sed -i '/ble_stub.c/d' "$STD_DIR/CMakeLists.txt" 2>/dev/null || true
}
trap cleanup EXIT

# 1. Overlay our app sources.
cp "$OVERLAY/app/ble_stub.c" "$STD_DIR/"

# 2. Patch standard_porting CMakeLists to compile ble_stub.c (idempotent).
if ! grep -q "ble_stub.c" "$STD_DIR/CMakeLists.txt"; then
    sed -i '/startup.S/a\    ${CMAKE_CURRENT_SOURCE_DIR}/ble_stub.c' "$STD_DIR/CMakeLists.txt"
fi

# 3. Build.
cd "$SDK"
"$PYTHON" build.py "$TARGET"

# 4. Collect firmware.
mkdir -p "$OUTPUT"
cp "$SDK"/output/bs21/fwpkg/"$TARGET"/*.fwpkg "$OUTPUT/" 2>/dev/null || true
echo "Firmware artifacts:"
ls -la "$OUTPUT"/*.fwpkg 2>/dev/null || echo "  (no fwpkg found)"
