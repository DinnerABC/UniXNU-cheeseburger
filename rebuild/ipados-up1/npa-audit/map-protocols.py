#!/usr/bin/env python3
"""Map real firmware protocol GUIDs (from .depex expressions) to the drivers
that carry them, so the NPA/PMIC/GLink interfaces can be identified without
their source headers.

Run inside the firmware VM:
  python3 map-protocols.py /home/<user>/unixnu-up1-<candidate>/source \
      /project/UniXNU-cheeseburger/out/p21/npa-audit/protocol-map.txt
"""
import struct
import sys
from pathlib import Path

TARGETS = ['NpaDxe.efi', 'GlinkDxe.efi', 'PmicDxe.efi', 'SPMI.efi', 'ClockDxe.efi']


def fmt(b):
    d1, d2, d3 = struct.unpack_from('<IHH', b, 0)
    return '%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X' % (
        d1, d2, d3, *b[8:16])


def depex_guids(data):
    out = []
    i = 0
    while i < len(data):
        if data[i] == 0x02 and i + 17 <= len(data):
            out.append(fmt(data[i + 1:i + 17]))
            i += 17
        else:
            i += 1
    return out


def main():
    tree = Path(sys.argv[1]).resolve()
    out_path = Path(sys.argv[2])
    bins = tree / 'Platforms/Msm8998Binaries/common/Binaries'
    real = {}
    for depex in bins.rglob('*.depex'):
        try:
            data = depex.read_bytes()
        except OSError:
            continue
        if depex.stat().st_size > 4096:
            continue
        for g in depex_guids(data):
            real.setdefault(g, set()).add(depex.parent.name)

    blobs = {}
    for name in TARGETS:
        p = bins / name.replace('.efi', '') / name
        if p.is_file():
            blobs[name] = p.read_bytes()

    lines = []
    lines.append(f'{len(real)} protocol GUIDs referenced by {len(list(bins.rglob("*.depex")))} depex files')
    lines.append('')
    lines.append('GUIDs referenced by depex AND present in the driver images:')
    for g in sorted(real):
        raw = struct.pack('<IHH8B', *[int(x, 16) for x in
                                     (g[0:8], g[9:13], g[14:18])] + [int(g[19:21], 16), int(g[21:23], 16),
                                                                      int(g[24:26], 16), int(g[26:28], 16),
                                                                      int(g[28:30], 16), int(g[30:32], 16),
                                                                      int(g[32:34], 16), int(g[34:36], 16)])
        hits = [n for n, b in blobs.items() if raw in b]
        if hits:
            lines.append(f'  {g} depex_of={sorted(real[g])} in={hits}')
    lines.append('')
    lines.append('Protocols whose only depex reference is one of the target drivers:')
    for g in sorted(real):
        names = real[g]
        if names and all(n in {t.replace('.efi', '') for t in TARGETS} for n in names):
            lines.append(f'  {g} only_in={sorted(names)}')
    out_path.write_text('\n'.join(lines) + '\n')
    print('\n'.join(lines[:40]))
    print('written', out_path)


if __name__ == '__main__':
    main()
