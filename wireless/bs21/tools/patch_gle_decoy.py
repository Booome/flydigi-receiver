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

PATCHES = [
    # (unique_context, offset_within_context, original_bytes, new_bytes, description)
    (
        bytes.fromhex('85477eac7e2c1387'),
        0,
        bytes.fromhex('8547'),
        bytes.fromhex('9147'),
        'add_service_core: first handle 1 -> 0x10',
    ),
    (
        bytes.fromhex('f9bf5049930700100145e3fa'),
        4,
        bytes.fromhex('93070010'),
        bytes.fromhex('9307ffff'),
        'check_property_info: remove oper<=256 cap',
    ),
]


def main(path):
    with open(path, 'rb') as f:
        data = f.read()

    for ctx, off, orig, new, desc in PATCHES:
        count = data.count(ctx)
        if count != 1:
            print(f'[ERROR] context for "{desc}" found {count} times (expected 1)')
            return 1
        pos = data.find(ctx) + off
        if data[pos:pos + len(orig)] != orig:
            print(f'[ERROR] original bytes mismatch at 0x{pos:x} for "{desc}"')
            return 1
        data = data[:pos] + new + data[pos + len(orig):]
        print(f'[OK] {desc}: patched {len(orig)} bytes at 0x{pos:x}')

    with open(path, 'wb') as f:
        f.write(data)
    print(f'[DONE] {path} patched')
    return 0


if __name__ == '__main__':
    target = sys.argv[1] if len(sys.argv) > 1 else \
        '/home/bodong/.local/Ai-BS21_SDK/protocol/bt/host/gle/' \
        'bs21-n1100-sle-peripheral-decoy/libbth_gle.a'
    sys.exit(main(target))
