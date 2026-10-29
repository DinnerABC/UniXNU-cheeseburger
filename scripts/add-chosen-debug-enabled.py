#!/usr/bin/env python3
"""Add /chosen/debug-enabled to the Stage C ADT.

pexpert/arm/pe_init.c copies /chosen/debug-enabled into debug_enabled; the
boot argument does not touch it. IOMFB's engineering display operations check
PE_i_can_has_debugger() and refuse with kIOReturnNotPermitted while it is 0,
which is what selector 15 reported on every boot so far.
"""
from pathlib import Path
import hashlib
import json
import struct
import sys

R = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(R / 'tools/adt'))
from adt import decode, encode  # noqa: E402

STAGE = R / 'out/p23/ios-16.7.16-20H392'
ADT = STAGE / 'adt/kernel.macho.adt'
BACKUP = STAGE / 'adt/kernel.macho.adt.before-debug-enabled'
REPORT = STAGE / 'adt/validation.json'
VALUE = struct.pack('<I', 1)


def emit(node):
    return encode(node['name'],
                  {k: v for k, v in node['properties'].items() if k != 'name'},
                  [emit(child) for child in node['children']])


def main():
    old = ADT.read_bytes()
    report = json.loads(REPORT.read_text())
    assert report['sha256'] == hashlib.sha256(old).hexdigest(), 'ADT does not match its record'
    tree = decode(old)
    chosen = [c for c in tree['children'] if c['name'] == 'chosen']
    assert len(chosen) == 1, 'ADT has no unique /chosen'
    if not BACKUP.exists():
        BACKUP.write_bytes(old)

    chosen[0]['properties']['debug-enabled'] = VALUE

    new = emit(tree)
    again = decode(new)
    got = next(c for c in again['children'] if c['name'] == 'chosen')['properties']['debug-enabled']
    assert got == VALUE

    ADT.write_bytes(new)
    report['sha256'] = hashlib.sha256(new).hexdigest()
    report['bytes'] = len(new)
    report['chosen_debug_enabled'] = {
        'node': '/chosen',
        'property': 'debug-enabled',
        'value': 1,
        'source': 'pexpert/arm/pe_init.c copies /chosen/debug-enabled into debug_enabled',
    }
    REPORT.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'sha256': report['sha256'], 'bytes': report['bytes']}, indent=2))


if __name__ == '__main__':
    main()