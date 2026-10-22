#!/usr/bin/env python3
"""Incremental P4 overlay on the retained P3 diagnostic workspace; reversible patch."""
from pathlib import Path
import shutil,difflib,hashlib,json
r=Path(__file__).resolve().parents[1];s=Path('/home/dinnerabc.guest/unixnu-p3/diagnostic')
assert (s/'P3Integration/Probe/P3Probe.inf').exists()
shutil.copytree(r/'config/p4/AutoReturn',s/'P4Integration/AutoReturn',dirs_exist_ok=True)
shutil.copytree(r/'config/p4/Probe',s/'P4Integration/Probe',dirs_exist_ok=True)
base='Platforms/Msm8998Pkg/Device/oneplus/cheeseburger/';patch=[]
for name in ['cheeseburger.dsc','cheeseburger.fdf']:
 p=s/(base+name);before=p.read_text();after=before
 if 'P4Integration/AutoReturn/P4AutoReturn.inf' not in before:
  if name.endswith('dsc'):after+='\n[Components]\n  P4Integration/AutoReturn/P4AutoReturn.inf\n'
  else:after=after.replace('    INF P3Integration/Probe/P3Probe.inf','    INF P4Integration/AutoReturn/P4AutoReturn.inf\n    INF P3Integration/Probe/P3Probe.inf')
  p.write_text(after)
  patch.extend(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='a/'+base+name,tofile='b/'+base+name))
for name in ['cheeseburger.dsc','cheeseburger.fdf']:
 p=s/(base+name);before=p.read_text();after=before.replace('P3Integration/Probe/P3Probe.inf','P4Integration/Probe/P4Probe.inf')
 if after!=before:
  p.write_text(after);patch.extend(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='a/'+base+name,tofile='b/'+base+name))
if patch:(r/'patches/p4/uefi-auto-return.patch').write_text(''.join(patch))
(r/'out/p4/auto-return-inputs.json').write_text(json.dumps({str(p.relative_to(r)):hashlib.sha256(p.read_bytes()).hexdigest() for p in (r/'config/p4/AutoReturn').glob('*')},indent=2)+'\n')
