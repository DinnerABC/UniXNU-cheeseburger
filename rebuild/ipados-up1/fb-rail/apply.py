#!/usr/bin/env python3
"""Install the touch-rail enable into a UniXNU firmware source tree.

Usage: python3 apply.py /home/<user>/unixnu-up1-<candidate>/source

The tree is the overlay the firmware build reads; this script only edits the
project-owned UniXNUPkg boot driver and is idempotent.
"""
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parent / 'UniXNURails.c'
TREE = Path(sys.argv[1]).resolve()
DRV = TREE / 'UniXNUPkg/Driver/UniXNUDxe'
INF = DRV / 'UniXNUDxe.inf'
C = DRV / 'UniXNUDxe.c'

assert SRC.is_file() and INF.is_file() and C.is_file(), (SRC, INF, C)

# 1. source file
(DRV / 'UniXNURails.c').write_bytes(SRC.read_bytes())

# 2. INF: compile it, link IoLib for MmioRead32/MmioWrite32
inf = INF.read_text()
if 'UniXNURails.c' not in inf:
    old = '  UniXNUDxe.c\n'
    assert old in inf, 'Sources block not found'
    inf = inf.replace(old, old + '  UniXNURails.c\n', 1)
if 'gEfiBlockIoProtocolGuid' not in inf:
    old = '  gEfiLoadedImageProtocolGuid         ## CONSUMES\n'
    assert old in inf, 'Protocols block not found'
    inf = inf.replace(old, old + '  gEfiBlockIoProtocolGuid             ## CONSUMES\n', 1)
if '\n  IoLib\n' not in inf:
    old = '  DebugLib\n'
    assert old in inf, 'LibraryClasses block not found'
    inf = inf.replace(old, old + '  IoLib\n', 1)
INF.write_text(inf)

# 3. entry point: enable the rails before the boot option is registered
c = C.read_text()
if 'UniXnuEnableTouchRails' not in c:
    anchor = '#include <Protocol/LoadedImage.h>\n'
    assert anchor in c, 'include block not found'
    if not c.startswith('#include <Uefi.h>') and '#include <Uefi.h>' not in c:
        c = '#include <Uefi.h>\n' + c
    c = c.replace(
        anchor,
        anchor + '\nextern VOID UniXnuEnableTouchRails (VOID);\n',
        1,
    )
    call = '  Path = FindApplication (ImageHandle);\n'
    assert call in c, 'entry body anchor not found'
    c = c.replace(call, '  UniXnuEnableTouchRails ();\n\n' + call, 1)
C.write_text(c)

print('rail enable installed in', DRV)
