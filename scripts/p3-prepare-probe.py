#!/usr/bin/env python3
"""Apply a read-only protocol screen probe to an independent P2 integrated copy."""
import argparse,difflib,shutil,json,hashlib,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('source',type=Path);a=p.parse_args()
r=Path(__file__).resolve().parents[1];s=a.source.resolve()
assert (s/'P2Integration/UniXNU.efi').is_file()
assert not (s/'P3Integration').exists(),'Probe overlay already exists'
shutil.copytree(r/'config/p3/Probe',s/'P3Integration/Probe')
changes=[]
base='Platforms/Msm8998Pkg/Device/oneplus/cheeseburger/'
for name in ['cheeseburger.dsc','cheeseburger.fdf']:
 f=s/(base+name);before=f.read_text()
 if name.endswith('dsc'):after=before+'\n[Components]\n  P3Integration/Probe/P3Probe.inf\n'
 else:
  anchor='    INF ShellPkg/Application/Shell/Shell.inf';assert before.count(anchor)==1
  after=before.replace(anchor,'    INF P3Integration/Probe/P3Probe.inf\n'+anchor)
 f.write_text(after)
 changes.extend(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='a/'+base+name,tofile='b/'+base+name))
(r/'patches/p3/runtime-probe.patch').write_text(''.join(changes))
(r/'out/p3/probe-inputs.json').write_text(json.dumps(dict(source=str(s),upstream_commit=subprocess.check_output(['git','-C',str(s),'rev-parse','HEAD'],text=True).strip(),files={str(f.relative_to(r)):hashlib.sha256(f.read_bytes()).hexdigest() for f in (r/'config/p3/Probe').glob('*')},base='P2 integrated tree; original application and registrar unchanged; P3 DXE added'),indent=2)+'\n')
