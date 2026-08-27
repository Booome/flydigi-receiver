#!/usr/bin/env python3
"""Patch libbth_gle.a for the Flydigi decoy (board_b).

All patches bring the stock BS21 stack behaviour back to the SSAP protocol
standard (the same behaviour the open-source NearLink stack implements):

1. Handle base 0x10: the open-source stack starts service handles at
   SSAP_INIT_HANDLE = 0x0010 (ssaps_service.c). The BS21 library initialises
   the handle cursor to 1 in three places; each is patched to match:
   - ssaps_add_service_core / ssaps_add_property_core: c.li a5,1 -> 16
   - ssaps_register_server: stored word now gives cursor=0 so the
     "cursor==0 -> 16" branch above fires for the first service
   - cs_range_allocate: first candidate handle 1 -> 0x10 (ATT-layer cursor)

2. check_property_info oper cap: the stock check rejects
   operate_indication > 0x100. operate_indication is a 32-bit field; the
   real controller uses 0x30d. Patch li a5,256 -> li a5,-1 so the unsigned
   compare always passes.

NOTE: the older objcopy --redefine-sym scheme was REMOVED. check_property_info
is a static symbol; renaming it never made the app-provided replacement link
(verified: the app copy was gc-sections stripped). The byte patch above is the
only effective mechanism.
"""

import sys
import os
import shutil

SDK_GLE = ('/home/bodong/.local/Ai-BS21_SDK/protocol/bt/host/gle/'
           'bs21-n1100-sle-peripheral/libbth_gle.a')

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
    # Emit a 2-byte uuid in each find-rsp item (matches the real controller's
    # 9-byte item layout: hdl + xx + oper + desc_cnt) instead of the full
    # 16-byte uuid. The 2-byte value is read from the LAST two bytes of the
    # registered uuid (u14,u15), which is where decoy_add_uuid16 places it.
    # NOTE: the older node+4 -> node+5 offset patch was a sham (it read a
    # fixed offset so the value showed 37be regardless of registration); it is
    # intentionally NOT re-added. See docs/ssap-uuid-false-fix.md.
    (
        bytes.fromhex('8280c14b894d1d'),
        1,
        2,
        bytes.fromhex('c14b'),
        bytes.fromhex('894b'),
        'find-rsp: property entry uuid 16 -> 2 (value read from u14/u15)',
    ),
]


def main(path):
    # Fresh copy from the pristine SDK library on every run (idempotent).
    if not os.path.exists(SDK_GLE):
        print(f'[ERROR] SDK library not found: {SDK_GLE}')
        return 1
    shutil.copyfile(SDK_GLE, path)

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
    print(f'[DONE] {path} patched (fresh copy from SDK + bytes)')
    return 0


if __name__ == '__main__':
    target = sys.argv[1] if len(sys.argv) > 1 else \
        '/home/bodong/.local/Ai-BS21_SDK/protocol/bt/host/gle/' \
        'bs21-n1100-sle-peripheral-decoy/libbth_gle.a'
    sys.exit(main(target))
