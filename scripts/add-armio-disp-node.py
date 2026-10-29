#!/usr/bin/env python3
"""Add the display canvas node and canvas size to the Stage C ADT.

ios-sweet (same 20H392 userland, GUI demonstrated) shows /arm-io/disp0 with
device_type=display-subsystem, compatible=sweet,display, canvas-width=1125 and
canvas-height=2436; IOMobileFramebuffer.framework counts those children to learn
the presented iPhone X canvas. PosterBoard/CollectionsPoster also read
canvas-width/canvas-height from the platform device itself, so the same pair is
published on the root node.
"""
from pathlib import Path
import hashlib
import json
import struct
import sys

R = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(R / 'tools/adt'))
from adt import decode, encode  # noqa: E402

STAGE = R / 'out/p23/ios-16.7.16-20H392'
ADT = STAGE / 'adt/kernel.macho.adt'
BACKUP = STAGE / 'adt/kernel.macho.adt.before-disp-node'
REPORT = STAGE / 'adt/validation.json'
CANVAS_W = struct.pack('<I', 1125)
CANVAS_H = struct.pack('<I', 2436)


def emit(node):
    return encode(node['name'],
                  {k: v for k, v in node['properties'].items() if k != 'name'},
                  [emit(child) for child in node['children']])


def main():
    old = ADT.read_bytes()
    old_sha = hashlib.sha256(old).hexdigest()
    report = json.loads(REPORT.read_text())
    assert report['sha256'] == old_sha, 'ADT does not match its validation record'
    tree = decode(old)
    armio = [c for c in tree['children'] if c['name'] == 'arm-io']
    assert len(armio) == 1, 'ADT has no unique /arm-io'
    if any(c['name'] == 'disp0' for c in armio[0]['children']):
        raise SystemExit('disp0 already present; nothing to do')
    if not BACKUP.exists():
        BACKUP.write_bytes(old)

    armio[0]['children'].append({
        'name': 'disp0',
        'properties': {
            'name': b'disp0\0',
            'device_type': b'display-subsystem\0',
            'compatible': b'sweet,display\0',
            'canvas-width': CANVAS_W,
            'canvas-height': CANVAS_H,
        },
        'children': [],
    })
    tree['properties']['canvas-width'] = CANVAS_W
    tree['properties']['canvas-height'] = CANVAS_H

    new = emit(tree)
    again = decode(new)
    assert any(c['name'] == 'disp0' for c in next(n for n in again['children'] if n['name'] == 'arm-io')['children'])
    assert again['properties']['canvas-width'] == CANVAS_W
    ADT.write_bytes(new)
    report['sha256'] = hashlib.sha256(new).hexdigest()
    report['bytes'] = len(new)
    report['canvas'] = {'width': 1125, 'height': 2436, 'node': '/arm-io/disp0', 'root_copy': True}
    report['display_canvas_source'] = 'ios-sweet mkadt (same 20H392 target)'
    REPORT.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
