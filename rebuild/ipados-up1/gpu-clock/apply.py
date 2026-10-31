#!/usr/bin/env python3
"""Install the read-only GPU clock inventory into a UniXNU firmware tree.

Usage: python3 apply.py /home/<user>/unixnu-up1-<candidate>/source

Idempotent. It copies EFIClock.h and UniXNUGpuClock.c into the project-owned
UniXNUDxe driver, makes UniXNURails' RailWriteCapture visible to it, adds the
source to the INF, and calls the probe from the driver entry after the touch
rails. It does not enable any clock or power domain.
"""
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
TREE = Path(sys.argv[1]).resolve()
DRV = TREE / 'UniXNUPkg/Driver/UniXNUDxe'
INF = DRV / 'UniXNUDxe.inf'
C = DRV / 'UniXNUDxe.c'
RAILS = DRV / 'UniXNURails.c'

assert (HERE / 'UniXNUGpuClock.c').is_file()
assert (HERE / 'EFIClock.h').is_file()
assert INF.is_file() and C.is_file() and RAILS.is_file(), (INF, C, RAILS)

(DRV / 'UniXNUGpuClock.c').write_bytes((HERE / 'UniXNUGpuClock.c').read_bytes())
(DRV / 'EFIClock.h').write_bytes((HERE / 'EFIClock.h').read_bytes())

inf = INF.read_text()
if 'UniXNUGpuClock.c' not in inf:
    anchor = '  UniXNURails.c\n'
    assert anchor in inf, 'UniXNURails.c not in INF'
    inf = inf.replace(anchor, anchor + '  UniXNUGpuClock.c\n', 1)
    INF.write_text(inf)

rails = RAILS.read_text()
new = re.sub(r'STATIC(\s+)EFI_STATUS(\s+)RailWriteCapture', r'EFI_STATUS\2RailWriteCapture', rails, count=1)
if new != rails:
    RAILS.write_text(new)

c = C.read_text()
if 'UniXnuGpuClockProbe' not in c:
    anchor = 'extern VOID UniXnuEnableTouchRails (VOID);\n'
    assert anchor in c, 'rail extern not found'
    c = c.replace(anchor, anchor + '\nextern VOID UniXnuGpuClockProbe (VOID);\n', 1)
    call = '  UniXnuEnableTouchRails ();\n'
    assert call in c, 'rail call not found'
    c = c.replace(call, call + '  UniXnuGpuClockProbe ();\n', 1)
if 'UniXnuGpuPowerEnable' not in c:
    anchor = 'extern VOID UniXnuGpuClockProbe (VOID);\n'
    assert anchor in c, 'clock extern not found'
    c = c.replace(anchor, anchor + '\nextern VOID UniXnuGpuPowerEnable (VOID);\n', 1)
    call = '  UniXnuGpuClockProbe ();\n'
    assert call in c, 'clock call not found'
    c = c.replace(call, call + '  UniXnuGpuPowerEnable ();\n', 1)
C.write_text(c)

print('gpu clock inventory installed in', DRV)
