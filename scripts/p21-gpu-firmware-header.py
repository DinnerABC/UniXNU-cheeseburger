#!/usr/bin/env python3
"""Pack the three A540 firmware blobs into the sagit QGFW container and emit a
private C header for the board runtime. The blobs stay under out/p21/gpu and
are never committed; the repository driver includes the header only when the
build tree provides it."""
from pathlib import Path
import struct
import sys

r = Path(__file__).resolve().parents[1]
fw = r / 'out/p21/gpu/firmware'
names = ['a530_pm4.fw', 'a530_pfp.fw', 'a540_gpmu.fw2']
blobs = [(fw / n).read_bytes() for n in names]

entries = []
off = 32 + 3 * 16
for i, b in enumerate(blobs):
    off = (off + 15) & ~15
    entries.append((i, off, len(b)))
    off += len(b)
total = off
container = bytearray(total)
struct.pack_into('<8I', container, 0, 0x57464751, 1, 3, total, 16, 0, 0, 0)
for i, (idx, at, length) in enumerate(entries):
    struct.pack_into('<4I', container, 32 + i * 16, idx, at, length, 0)
for (idx, at, length), b in zip(entries, blobs):
    container[at:at + length] = b

lines = ['/* Generated from the device vendor files by scripts/p21-gpu-firmware-header.py. */',
         'static const unsigned char gpu_firmware_blob[%d] = {' % len(container)]
for i in range(0, len(container), 16):
    lines.append('  ' + ','.join('0x%02x' % x for x in container[i:i + 16]) + ',')
lines.append('};')
lines.append('static const unsigned gpu_firmware_size = %d;' % len(container))
text = '\n'.join(lines) + '\n'

targets = []
for base in (Path('/Users/dinnerabc/unixnu-p21/fb-qup3/source-tree'), r / 'out/p21/fb-qup3/source-tree'):
    if base.is_dir():
        targets.append(base / 'package/BoardRuntime/CheeseburgerGpuFirmware.h')
for target in targets:
    target.write_text(text)
    print('wrote', target, len(container), 'bytes')
if not targets:
    print('no build tree found', file=sys.stderr)
    sys.exit(1)
