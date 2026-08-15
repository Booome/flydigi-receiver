#!/usr/bin/env bash
# Build the Flydigi BS21 firmware by overlaying our config onto the external
# fbb_bs2x SDK and invoking its build system. The SDK itself is never modified
# in place for our changes; this script copies our overlay into it at build time.
set -euo pipefail

SDK="${FBB_BS2X_PATH:-$HOME/.local/fbb_bs2x}"
SDK_SRC="$SDK/src"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OVERLAY="$ROOT/overlay"
OUTPUT="$ROOT/output"

PYTHON="${PYTHON:-python3}"
TARGET="flydigi-bs21e"
CHIP="bs21e"
CORE="acore"

if [ ! -d "$SDK_SRC" ]; then
    echo "error: SDK not found at $SDK (set FBB_BS2X_PATH)" >&2
    exit 1
fi

# 1. Apply overlay (add our target dir + menuconfig file; upstream untouched)
cp -r "$OVERLAY/target_config/flydigi" \
      "$SDK_SRC/build/config/target_config/"
cp "$OVERLAY/menuconfig/flydigi_bs21e.config" \
   "$SDK_SRC/build/config/target_config/$CHIP/menuconfig/$CORE/"

# 1b. Reuse standard's closed-source prebuilt libs (per-target dirs) via symlink.
# fbb_bs2x ships proprietary blobs (BT stack, USB, LiteOS, NFC, NV, partition...)
# under per-target directories. Our derived target has no such blobs, so point
# every <parent>/standard-bs21e-1100e at a same-named flydigi-bs21e link.
while IFS= read -r d; do
    parent="$(dirname "$d")"
    link="$parent/flydigi-bs21e"
    if [ ! -e "$link" ]; then
        ln -s "standard-bs21e-1100e" "$link"
    fi
done < <(find "$SDK_SRC" -type d -name "standard-bs21e-1100e" -not -path "*/output/*")

# 1c. Generate sign config for our target. The upstream cfg hard-codes the
# standard target name in its SrcFile/DstFile paths, so derive a copy.
sign_dir="$SDK_SRC/build/config/target_config/$CHIP/sign_config"
sign_src="$sign_dir/standard_bs21e_1100e.cfg"
sign_dst="$sign_dir/flydigi_bs21e.cfg"
if [ -L "$sign_dst" ]; then
    rm -f "$sign_dst"
fi
sed 's/standard-bs21e-1100e/flydigi-bs21e/g' "$sign_src" > "$sign_dst"

# 2. Build
cd "$SDK_SRC"
"$PYTHON" build.py -c "$TARGET"

# 3. Collect firmware
mkdir -p "$OUTPUT"
cp "$SDK_SRC"/output/"$CHIP"/fwpkg/"$TARGET"/*.fwpkg "$OUTPUT/" 2>/dev/null || true
echo "Firmware artifacts:"
ls -la "$OUTPUT"/*.fwpkg 2>/dev/null || echo "  (no fwpkg found)"
