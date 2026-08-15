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
DEMO_DIR="$SDK/application/demo"
CONFIG_PY="$SDK/build/config/target_config/bs21/config.py"

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
    if [ -f "$DEMO_DIR/demo.c.bak" ]; then
        mv "$DEMO_DIR/demo.c.bak" "$DEMO_DIR/demo.c"
    fi
    sed -i "s/'CONFIG_BT_SLE_ONLY', 'NO_BOOT_BACKUP'/'CONFIG_BT_SLE_ONLY'/" "$CONFIG_PY" 2>/dev/null || true
}
trap cleanup EXIT

# 1. Overlay ble_stub.c (SLE-only libbth_sdk still references BLE sapi symbols).
cp "$OVERLAY/app/ble_stub.c" "$STD_DIR/"

# 2. Patch standard_porting CMakeLists to compile ble_stub.c (idempotent).
if ! grep -q "ble_stub.c" "$STD_DIR/CMakeLists.txt"; then
    sed -i '/startup.S/a\    ${CMAKE_CURRENT_SOURCE_DIR}/ble_stub.c' "$STD_DIR/CMakeLists.txt"
fi

# 3. Replace demo.c with our overlay (adds app_run(axk_main) so the demo runs).
cp "$DEMO_DIR/demo.c" "$DEMO_DIR/demo.c.bak"
cp "$OVERLAY/app/demo.c" "$DEMO_DIR/demo.c"

# 4. Fix rcu flash-layout bug: bs21-rcu partition table has no flashboot_backup,
#    but the linker script still uses the standard layout (application @ 0x15000
#    instead of 0xb000), so flashboot jumps 0xA000 too far. Add NO_BOOT_BACKUP.
sed -i "s/'CONFIG_BT_SLE_ONLY'/'CONFIG_BT_SLE_ONLY', 'NO_BOOT_BACKUP'/" "$CONFIG_PY"

# 5. Build.
cd "$SDK"
"$PYTHON" build.py "$TARGET"

# 6. Collect firmware.
mkdir -p "$OUTPUT"
cp "$SDK"/output/bs21/fwpkg/"$TARGET"/*.fwpkg "$OUTPUT/" 2>/dev/null || true
echo "Firmware artifacts:"
ls -la "$OUTPUT"/*.fwpkg 2>/dev/null || echo "  (no fwpkg found)"
