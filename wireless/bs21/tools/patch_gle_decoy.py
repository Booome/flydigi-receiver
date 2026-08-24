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
        bytes.fromhex('c147'),
        'first service/property handle 1 -> 0x10',
    ),
    (
        bytes.fromhex('f9bf5049930700100145e3fa'),
        1,
        4,
        bytes.fromhex('93070010'),
        bytes.fromhex('9307f0ff'),
        'check_property_info: remove oper<=256 cap',
    ),
    # register_server init: srli(a5=-1,15)=0x0001FFFF stored as one word at
    # server+28, giving state(+28)=0xFFFF and handle cursor(+30)=1. Replace
    # the srli with c.li a5,1 + nop so the stored word is 1: state=1 (normal
    # path) and cursor=0, which makes the patched "cursor==0 -> 16" branch
    # in add_service_core fire for the FIRST service only.
    (
        bytes.fromhex('fd57bd835ccc'),
        1,
        2,
        bytes.fromhex('bd83'),
        bytes.fromhex('8547'),
        'register_server: handle cursor init 1 -> 0',
    ),
    # cs_range_allocate: first candidate handle starts at 1. The SSAP
    # mirror layer and the cs (ATT server) tree allocate handles
    # independently, so BOTH must be moved to 0x10 or discovery responses
    # show handles shifted by -0x0F vs the app-visible ones.
    # c.li s2,1 -> c.li s2,16 (rd=x18, imm=16).
    (
        bytes.fromhex('0149b5a80041054905e4c167'),
        1,
        6,
        bytes.fromhex('0549'),
        bytes.fromhex('4149'),
        'cs_range_allocate: first db handle 1 -> 0x10',
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
