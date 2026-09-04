#!/usr/bin/env python3
"""Official WS63 advertising-channel fix: set NV btc_channel_scan_switch = 0.

Chain (reverse-engineered from libbgtp.a + the WS63 NV config):
  NV key 0x20AB (btc_channel_scan_switch)
    -> bt_customize_load -> uapi_nv_read -> g_bt_customize.byte23
    -> bt_customize_support_config: byte23==0 clears bit2 of g_macro_cfg_flags
    -> lm_chnl_scan_get_gle_chnl_map: (flags & 0x04)==0 skips AND-ing the host
       advertising channel_map with the coex air_used channel map
    -> advertising honours the full channel_map (multi-channel) instead of being
       masked to a single channel by WiFi/BT coexistence.

The SDK default is value:1 (masking enabled). This patch flips it to 0 in the
per-core NV config JSONs consumed by the build (cfg/acore/app.json, perf.json).
It edits a documented config constant (not a binary), keeps a one-time .orig
backup per file, and supports --restore.
"""

import argparse
import glob
import os
import re
import shutil
import sys

CFG_REL = "middleware/chips/ws63/nv/nv_config/cfg/acore/*.json"
KEY = "btc_channel_scan_switch"
# match: "<KEY>":{ ... "value": <N>   (bounded to the object, no nested braces)
BLOCK = re.compile(r'("%s"\s*:\s*\{[^{}]*?"value"\s*:\s*)(\d+)' % re.escape(KEY))
WANT = "0"


def default_sdk_dir():
    return os.environ.get("FBB_SDK_DIR", os.path.expanduser("~/workspace/fbb_ws63/src"))


def _targets(root):
    return sorted(glob.glob(os.path.join(root, CFG_REL)))


def apply(root):
    """Flip btc_channel_scan_switch value to 0 across the core NV JSONs."""
    results = []
    for path in _targets(root):
        with open(path, "r", encoding="utf-8") as fh:
            text = fh.read()
        if KEY not in text:
            continue
        cur = BLOCK.search(text)
        if cur is None:
            results.append(f"{os.path.basename(path)}: {KEY} present but no value field")
            continue
        if cur.group(2) == WANT:
            results.append(f"{os.path.basename(path)}: already {WANT}")
            continue
        backup = path + ".orig"
        if not os.path.isfile(backup):
            shutil.copy2(path, backup)
        new_text = BLOCK.sub(lambda m: m.group(1) + WANT, text, count=1)
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(new_text)
        results.append(f"{os.path.basename(path)}: {KEY} {cur.group(2)} -> {WANT}")
    return "; ".join(results) if results else f"{KEY} not found in NV cfg JSONs"


def restore(root):
    """Restore stock NV JSONs from their .orig backups."""
    restored = []
    for path in _targets(root):
        backup = path + ".orig"
        if os.path.isfile(backup):
            shutil.copy2(backup, path)
            restored.append(os.path.basename(path))
    return f"restored {', '.join(restored)}" if restored else "nothing to restore"


def main():
    ap = argparse.ArgumentParser(description="WS63 advertising coex-channel NV patch")
    ap.add_argument("--restore", action="store_true")
    args = ap.parse_args()
    root = default_sdk_dir()
    if not os.path.isdir(root):
        sys.exit(f"error: SDK not found at {root} (set FBB_SDK_DIR)")
    print(restore(root) if args.restore else apply(root))


if __name__ == "__main__":
    main()
