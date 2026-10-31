#!/usr/bin/env python3
"""Self-check for the CBHB heartbeat record codec used by the log reader."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts/logging'))
import heartbeat_record as hb  # noqa: E402

failures = []


def check(cond, name):
    if not cond:
        failures.append(name)
        print('FAIL', name)


block = hb.encode(0x510EA570, 7, 123, 'tick', 2, 8)
r = hb.parse(block)
check(len(block) == 4096, 'block-size')
check(r is not None and r['seq'] == 7 and r['tag'] == 'tick', 'roundtrip')
check(r['boot_id'] == '0x00000000510ea570', 'boot-id')
check(hb.parse(b'') is None, 'empty')
check(hb.parse(b'XXXX' + b'\0' * 100) is None, 'wrong-magic')
check(hb.parse(block[:32]) is None, 'truncated')
check(hb.parse(hb.encode(1, 2, 3, 'smc', 2, 4))['tag'] == 'smc', 'smc-tag')
check(hb.parse(hb.encode(1, 2, 3, 'cm3', 2, 4))['tag'] == 'cm3', 'digit-tag')

if failures:
    print('RESULT FAIL', len(failures))
    sys.exit(1)
print('RESULT PASS 8/8')
