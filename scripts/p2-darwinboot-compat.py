#!/usr/bin/env python3
"""Use the pinned old EDK2's equivalent AArch64 assembly macro header."""
from pathlib import Path
import argparse,difflib
p=argparse.ArgumentParser();p.add_argument('source',type=Path);a=p.parse_args()
root=Path(__file__).resolve().parents[1];changes=[]
for rel,old,new in [
 ('DarwinBootPkg/Driver/Entry.S','#include <AsmMacroLib.h>','#include <AsmMacroIoLibV8.h>'),
 ('DarwinBootPkg/Driver/DarwinBoot.inf','  MdePkg/MdePkg.dec','  MdePkg/MdePkg.dec\n  ArmPkg/ArmPkg.dec'),
]:
 f=a.source/rel;before=f.read_text()
 if new in before and old not in before:continue
 if rel.endswith('.inf') and '  ArmPkg/ArmPkg.dec' in before:continue
 assert before.count(old)==1,rel
 after=before.replace(old,new)
 changes.extend(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='a/'+rel,tofile='b/'+rel))
 f.write_text(after)
if changes:(root/'patches/p2/darwinboot-old-edk2-asm.patch').write_text(''.join(changes))
