#!/usr/bin/env python3
"""Pack the vendor A540 ZAP files into the sagit QZAP container and emit the
private C header used by CheeseburgerGpuPas.cpp. The vendor bytes and the
generated header stay under out/ and are never committed; the repository only
contains the parser and the generator."""
import hashlib
import json
import struct
from pathlib import Path

R = Path(__file__).resolve().parents[1]
FW = R / 'out/p21/gpu/firmware'
NAMES = ['a540_zap.mdt', 'a540_zap.b00', 'a540_zap.b01', 'a540_zap.b02']
IDS = [0xFFFFFFFF, 0, 1, 2]

blobs = [(FW / n).read_bytes() for n in NAMES]
entries = []
off = 32 + len(blobs) * 16
for blob in blobs:
    off = (off + 15) & ~15
    entries.append(off)
    off += len(blob)
total = off
container = bytearray(total)
struct.pack_into('<8I', container, 0, 0x50415a51, 1, len(blobs), total, 16, 0, 0, 0)
for i, (ident, at) in enumerate(zip(IDS, entries)):
    struct.pack_into('<4I', container, 32 + i * 16, ident, at, len(blobs[i]), 0)
for at, blob in zip(entries, blobs):
    container[at:at + len(blob)] = blob

lines = ['/* Generated from the device vendor files by scripts/p21-zap-firmware-header.py. */',
         'static const unsigned char zap_firmware_blob[%d] = {' % len(container)]
for i in range(0, len(container), 16):
    lines.append('  ' + ','.join('0x%02x' % x for x in container[i:i + 16]) + ',')
lines.append('};')
lines.append('static const unsigned zap_firmware_size = %d;' % len(container))
text = '\n'.join(lines) + '\n'

targets = []
for base in (Path('/Users/dinnerabc/unixnu-p21/fb-qup3/source-tree'),
             R / 'out/p21/fb-qup3/source-tree'):
    if base.is_dir():
        targets.append(base / 'package/BoardRuntime/CheeseburgerZapFirmware.h')
for target in targets:
    target.write_text(text)
    print('wrote', target, len(container), 'bytes')
if not targets:
    print('no build tree found', file=__import__('sys').stderr)
    raise SystemExit(1)

receipt = {
    'result': 'ZAP_CONTAINER_PACKED',
    'container_bytes': len(container),
    'container_sha256': hashlib.sha256(container).hexdigest(),
    'entries': [{'id': ident, 'name': name, 'offset': at, 'bytes': len(blob),
                 'sha256': hashlib.sha256(blob).hexdigest()}
                for ident, name, at, blob in zip(IDS, NAMES, entries, blobs)],
    'hardware_tested': False,
}
(R / 'out/p21/gpu/zap-container.json').write_text(json.dumps(receipt, indent=2) + '\n')
print(json.dumps(receipt, indent=2))
