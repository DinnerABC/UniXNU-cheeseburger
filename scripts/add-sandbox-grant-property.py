#!/usr/bin/env python3
"""Add the native sandbox grant property to the display canvas nodes.

UniXNU's zzw/zzwb patches let a service publish NativeSandboxGrantProcess and
stand in for the sandbox verdict on that service's IOKit client and property
operations. The 20H392 wallpaper services read canvas-width/canvas-height
from the platform device; the sandbox profile names no such class and denies
the read. The same grant is published here, on the root platform node and on
/arm-io/disp0, so the get-property path can honour it.
"""
from pathlib import Path
import hashlib
import json
import sys

R = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(R / 'tools/adt'))
from adt import decode, encode  # noqa: E402

STAGE = R / 'out/p23/ios-16.7.16-20H392'
ADT = STAGE / 'adt/kernel.macho.adt'
BACKUP = STAGE / 'adt/kernel.macho.adt.before-sandbox-grant'
REPORT = STAGE / 'adt/validation.json'
PROPERTY = 'NativeSandboxGrantProcess'
CLIENTS = b'PosterBoard,CollectionsPoster,SpringBoard,backboardd\0'


def emit(node):
    return encode(node['name'],
                  {k: v for k, v in node['properties'].items() if k != 'name'},
                  [emit(child) for child in node['children']])


def main():
    old = ADT.read_bytes()
    report = json.loads(REPORT.read_text())
    assert report['sha256'] == hashlib.sha256(old).hexdigest(), 'ADT does not match its record'
    tree = decode(old)
    armio = [c for c in tree['children'] if c['name'] == 'arm-io']
    assert len(armio) == 1, 'ADT has no unique /arm-io'
    disp0 = [c for c in armio[0]['children'] if c['name'] == 'disp0']
    assert len(disp0) == 1, 'ADT has no /arm-io/disp0; run add-armio-disp-node.py first'
    if not BACKUP.exists():
        BACKUP.write_bytes(old)

    tree['properties'][PROPERTY] = CLIENTS
    disp0[0]['properties'][PROPERTY] = CLIENTS

    new = emit(tree)
    again = decode(new)
    assert again['properties'][PROPERTY] == CLIENTS
    assert any(c['name'] == 'disp0' and c['properties'][PROPERTY] == CLIENTS
               for c in next(n for n in again['children'] if n['name'] == 'arm-io')['children'])
    assert again['properties']['canvas-width'] == tree['properties']['canvas-width']

    ADT.write_bytes(new)
    report['sha256'] = hashlib.sha256(new).hexdigest()
    report['bytes'] = len(new)
    report['sandbox_grant'] = {
        'property': PROPERTY,
        'value': CLIENTS.rstrip(b'\0').decode(),
        'nodes': ['/', '/arm-io/disp0'],
        'source': 'UniXNU zzw/zzwb native sandbox grant interface',
    }
    REPORT.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'sha256': report['sha256'], 'bytes': report['bytes'],
                      'property': PROPERTY}, indent=2))


if __name__ == '__main__':
    main()