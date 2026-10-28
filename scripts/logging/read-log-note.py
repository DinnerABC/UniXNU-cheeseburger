#!/usr/bin/env python3
"""Read the panic-time note block at the end of the authorized log window. Read-only."""
from pathlib import Path
import sys
R = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(R / 'out/p21/r14/scripts'))
from device import Device

d = Device('persistent-log-note-read')
d.require_root_recovery()
out = d.shell('dd if=/dev/block/sda14 bs=4096 skip=2629631 count=1 2>/dev/null | strings | head -3')
print(out)
if 'CBNOTE01' not in out:
    raise SystemExit('NO_NOTE')
