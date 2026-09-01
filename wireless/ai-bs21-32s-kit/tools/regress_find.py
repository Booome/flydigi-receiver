#!/usr/bin/env python3
"""Regression check for the decoy find-rsp format (M8).

Parses a probe serial capture and compares the discovery output against the
real-controller baseline. Usage:

    python3 wireless/ai-bs21-32s-kit/tools/regress_find.py <probe_log.txt>

Baseline (experiment N, real controller, two rounds consistent):
  properties : hdl oper desc types uuid
    0x11     0x30d  1   [02]  len=2 37BE
    0x12     0x5    0   []    len=2 37BE
    0x13     0xd    0   []    len=2 37BE
    0x14     0x2    0   []    len=2 37BE
    0x16     0x1    0   []    len=2 37BE
    0x17     0x1    0   []    len=2 37BE
    0x18     0x1    0   []    len=2 37BE
  services   : start end uuid
    0x10     0x14   37BE
    0x15     0x18   37BE
"""

import re
import sys

BASELINE_PROPS = [
    ('0x11', '0x30d', '1', ['02'], '2', '37BE'),
    ('0x12', '0x5', '0', [], '2', '37BE'),
    ('0x13', '0xd', '0', [], '2', '37BE'),
    ('0x14', '0x2', '0', [], '2', '37BE'),
    ('0x16', '0x1', '0', [], '2', '37BE'),
    ('0x17', '0x1', '0', [], '2', '37BE'),
    ('0x18', '0x1', '0', [], '2', '37BE'),
]
BASELINE_SERVICES = [('0x10', '0x14', '37BE'), ('0x15', '0x18', '37BE')]

PROP_RE = re.compile(
    r'find_property: c=\d+ conn=\d+ hdl=(0x[0-9a-f]+) oper=(0x[0-9a-f]+) '
    r'desc_cnt=(\d+) types=\[([^\]]*)\]')
UUID_RE = re.compile(r'prop uuid: len=(\d+) ((?:[0-9A-F]{2})+)')
SVC_RE = re.compile(
    r'find_structure_cb: c=\d+ conn=\d+ status=0x0 start=(0x[0-9a-f]+) '
    r'end=(0x[0-9a-f]+) UUID=((?:[0-9A-F]{2})+)')


def parse(log):
    props, svcs = [], []
    lines = log.splitlines()
    for i, line in enumerate(lines):
        m = PROP_RE.search(line)
        if m:
            types = [t.strip() for t in m.group(4).split(',') if t.strip()]
            # uuid line follows
            um = UUID_RE.search(lines[i + 1]) if i + 1 < len(lines) else None
            if um:
                raw = bytes.fromhex(um.group(2))
                uuid = raw[:2].hex().upper()  # short form is 37be prefix
                props.append((m.group(1), m.group(2), m.group(3), types,
                              um.group(1), uuid))
        sm = SVC_RE.search(line)
        if sm:
            raw = bytes.fromhex(sm.group(3))
            svcs.append((sm.group(1), sm.group(2), raw[:2].hex().upper()))
    return props, svcs


def main(path):
    log = open(path, encoding='utf-8', errors='replace').read()
    props, svcs = parse(log)

    ok = True
    print(f'properties found: {len(props)} (baseline {len(BASELINE_PROPS)})')
    if not props:
        print('FAIL: no find_property entries captured')
        return 1
    for got, want in zip(props, BASELINE_PROPS):
        gh, go, gd, gt, glen, guuid = got
        wh, wo, wd, wt, wlen, wuuid = want
        match = (gh == wh and int(go, 16) == int(wo, 16) and gd == wd
                 and gt == wt and glen == wlen and guuid == wuuid)
        flag = 'OK  ' if match else 'DIFF'
        ok &= match
        exp = f'hdl={wh} oper={wo} desc={wd} types={wt} uuid len={wlen} {wuuid}'
        act = f'hdl={gh} oper={go} desc={gd} types={gt} uuid len={glen} {guuid}'
        print(f'  [{flag}] expect {exp} | got {act}')

    print(f'services found: {len(svcs)} (baseline {len(BASELINE_SERVICES)})')
    for got in svcs:
        gs, ge, gu = got
        want = BASELINE_SERVICES[len(svcs) - len(BASELINE_SERVICES):] \
            if False else None
        m = next((w for w in BASELINE_SERVICES if w[0] == gs), None)
        match = m is not None and m[1] == ge and m[2] == gu
        flag = 'OK  ' if match else 'DIFF'
        ok &= match
        print(f'  [{flag}] svc start={gs} end={ge} uuid={gu}')

    print('PASS' if ok else 'FAIL')
    return 0 if ok else 1


if __name__ == '__main__':
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1]))
