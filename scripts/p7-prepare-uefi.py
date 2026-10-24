#!/usr/bin/env python3
from pathlib import Path
import shutil,difflib
r=Path(__file__).resolve().parents[1];s=Path('/home/dinnerabc.guest/unixnu-p7/source');assert (s/'.p7-overlay-marker').exists()
shutil.copytree(r/'config/p7/Probe',s/'P7Integration/Probe',dirs_exist_ok=True)
shutil.copy2(r/'config/p7/Expected.h',s/'P7Integration/Probe/P7Expected.h')
shutil.copy2(r/'config/p7/Expected.h',s/'DarwinBootPkg/Driver/P7Expected.h')
base='Platforms/Msm8998Pkg/Device/oneplus/cheeseburger/'
for name in ['cheeseburger.dsc','cheeseburger.fdf']:
 p=s/(base+name);t=p.read_text().replace('P6Integration/Probe/P6Probe.inf','P7Integration/Probe/P7Probe.inf');p.write_text(t)
p=s/'DarwinBootPkg/Driver/DarwinBoot.c';before=p.read_text();after=before.replace('P6','P7');p.write_text(after)
if after!=before:(r/'patches/p7/darwinboot-diag-v1.patch').write_text(''.join(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='a/DarwinBootPkg/Driver/DarwinBoot.c',tofile='b/DarwinBootPkg/Driver/DarwinBoot.c')))
p=s/'DarwinBootPkg/Driver/DarwinBoot.inf';t=p.read_text().replace('  P6Expected.h','  P7Expected.h');p.write_text(t)
print('P7 UEFI expected hashes and versioned read-only register probe installed')
