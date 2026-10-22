#!/usr/bin/env python3
from pathlib import Path
import shutil,difflib
r=Path(__file__).resolve().parents[1];s=Path('/home/dinnerabc.guest/unixnu-p6/source');assert (s/'.p6-overlay-marker').exists()
shutil.copytree(r/'config/p6/Probe',s/'P6Integration/Probe',dirs_exist_ok=True)
shutil.copy2(r/'config/p6/Expected.h',s/'P6Integration/Probe/P6Expected.h')
shutil.copy2(r/'config/p6/Expected.h',s/'DarwinBootPkg/Driver/P6Expected.h')
base='Platforms/Msm8998Pkg/Device/oneplus/cheeseburger/'
for name in ['cheeseburger.dsc','cheeseburger.fdf']:
 p=s/(base+name);t=p.read_text().replace('P5Integration/Probe/P5Probe.inf','P6Integration/Probe/P6Probe.inf');p.write_text(t)
p=s/'DarwinBootPkg/Driver/DarwinBoot.c';before=p.read_text();after=before.replace('P5','P6');p.write_text(after)
if after!=before:(r/'patches/p6/darwinboot-diag-v1.patch').write_text(''.join(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='a/DarwinBootPkg/Driver/DarwinBoot.c',tofile='b/DarwinBootPkg/Driver/DarwinBoot.c')))
p=s/'DarwinBootPkg/Driver/DarwinBoot.inf';t=p.read_text().replace('  P5Expected.h','  P6Expected.h');p.write_text(t)
print('P6 UEFI expected hashes and versioned read-only register probe installed')
