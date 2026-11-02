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
# Tick submit fields: rc/stage/irq/cpirq/seq plus the pre-latch pair and fence.
line = ('CBHB0001 boot=%016x seq=9 sec=40 tag=tick state=2 rounds=7 m=1 l=3 o=1 s=7 a=1 '
        'f=-3/3/7e/0/2/200/0/1\n' % 0xAA).encode('ascii')
r = hb.parse(line + b'\0' * (4096 - len(line)))
check(r is not None and r.get('submit_rc') == -3 and r.get('submit_stage') == 3
      and r.get('submit_irq') == 0x7e and r.get('submit_seq') == 2
      and r.get('submit_preirq') == 0x200 and r.get('submit_precpirq') == 0
      and r.get('submit_fenced') == 1, 'tick/submit-fields')
# collect: per-boot filtering, foreign/empty accounting, gap detection
win = bytearray(4 * 4096)
win[0:4096] = hb.encode(0xAA, 1, 10, 'tick', 2, 3)
win[4096:8192] = hb.encode(0xAA, 2, 20, 'tick', 2, 3)
win[8192:12288] = hb.encode(0xBB, 1, 30, 'mark', 2, 3)
c = hb.collect(bytes(win), 0xAA, 0, 4)
check(len(c['records']) == 2 and c['foreign'] == 1 and c['empty'] == 1, 'collect/filter')
check(c['gaps'].get('tick', 0) == 0, 'collect/no-gap')
win[4096:8192] = hb.encode(0xAA, 3, 20, 'tick', 2, 3)
c = hb.collect(bytes(win), 0xAA, 0, 4)
check(c['gaps'].get('tick', 0) == 1, 'collect/gap')

if failures:
    print('RESULT FAIL', len(failures))
    sys.exit(1)
print('RESULT PASS 11/11')
