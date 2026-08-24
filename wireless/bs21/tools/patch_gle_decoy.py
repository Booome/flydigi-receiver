#!/usr/bin/env python3
"""Patch libbth_gle.a for the Flydigi decoy (board_b).

Two 2-byte instruction patches, both located by unique byte signatures:

1. ssaps_add_service_core: c.li a5,1 -> c.li a5,16
   First service handle becomes 0x10, matching the real controller table
   layout (svc0@0x10, props@0x11-0x14, svc1@0x15, props@0x16-0x18).

2. check_property_info: addi a5,x0,256 -> addi a5,x0,-1
   The oper_indication<=256 guard makes bgeu always true afterwards,
   so the real controller's oper value 0x30d (781) is accepted.
"""

import sys
import os
import shutil
import subprocess

SDK_GLE = ('/home/bodong/.local/Ai-BS21_SDK/protocol/bt/host/gle/'
           'bs21-n1100-sle-peripheral/libbth_gle.a')
OBJCOPY = ('/home/bodong/.local/Ai-BS21_SDK/tools/bin/compiler/riscv/'
           'cc_riscv32_musl_b010/cc_riscv32_musl_fp/bin/'
           'riscv32-linux-musl-objcopy')

# check_property_info is a static (local) symbol in the SDK library whose
# stock body caps operate_indication at 0x100. Rename it inside the decoy
# copy; the app provides decoy_check_property_info_orig as the replacement.
REDEFINES = [
    'check_property_info=decoy_check_property_info_orig',
]

PATCHES = [
    # (unique_context, expected_count, offset_within_context, original_bytes,
    #  new_bytes, description)
    (
        bytes.fromhex('85477eac7e2c1387'),
        2,   # service-core AND property-core each embed this init sequence;
             # the property-side copy is a dead path in our call order
        0,
        bytes.fromhex('8547'),
        bytes.fromhex('9147'),
        'first service/property handle 1 -> 0x10',
    ),
    (
        bytes.fromhex('f9bf5049930700100145e3fa'),
        1,
        4,
        bytes.fromhex('93070010'),
        bytes.fromhex('9307ffff'),
        'check_property_info: remove oper<=256 cap',
    ),
]


def main(path):
    # Fresh copy from the pristine SDK library on every run (idempotent).
    if not os.path.exists(SDK_GLE):
        print(f'[ERROR] SDK library not found: {SDK_GLE}')
        return 1
    shutil.copyfile(SDK_GLE, path)

    for redef in REDEFINES:
        ret = subprocess.run([OBJCOPY, f'--redefine-sym={redef}', path, path],
                             capture_output=True, text=True)
        if ret.returncode != 0:
            print(f'[ERROR] objcopy --redefine-sym {redef}: {ret.stderr.strip()}')
            return 1
        print(f'[OK] redefined {redef}')

    with open(path, 'rb') as f:
        data = f.read()

    for ctx, expect, off, orig, new, desc in PATCHES:
        count = data.count(ctx)
        if count != expect:
            print(f'[ERROR] context for "{desc}" found {count} times (expected {expect})')
            return 1
        patched = 0
        pos = data.find(ctx) + off
        while True:
            if data[pos:pos + len(orig)] != orig:
                print(f'[ERROR] original bytes mismatch at 0x{pos:x} for "{desc}"')
                return 1
            data = data[:pos] + new + data[pos + len(orig):]
            patched += 1
            nxt = data.find(ctx, pos)
            if nxt < 0:
                break
            pos = nxt + off
        print(f'[OK] {desc}: patched {patched} site(s)')

    with open(path, 'wb') as f:
        f.write(data)
    print(f'[DONE] {path} patched (fresh copy from SDK + redefine + bytes)')
    return 0


if __name__ == '__main__':
    target = sys.argv[1] if len(sys.argv) > 1 else \
        '/home/bodong/.local/Ai-BS21_SDK/protocol/bt/host/gle/' \
        'bs21-n1100-sle-peripheral-decoy/libbth_gle.a'
    sys.exit(main(target))
