#!/usr/bin/env python3
"""Add locked UniXNU startup components to an isolated MSM8998 source copy."""
import argparse,difflib,hashlib,json,shutil,subprocess,sys
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('--unixnu',type=Path,required=True);p.add_argument('--darwinboot',type=Path,required=True);p.add_argument('--efi',type=Path,required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[1];s=a.source.resolve()
for name,src in [('UniXNUPkg',a.unixnu/'package/UniXNUPkg'),('DarwinBootPkg',a.darwinboot/'DarwinBootPkg')]:
 if (s/name).exists():raise SystemExit(f'Refusing to overwrite {s/name}')
 shutil.copytree(src,s/name)
overlay=s/'P2Integration';overlay.mkdir()
shutil.copy2(a.efi,overlay/'UniXNU.efi')
inc='''# P2 source-built registrar and loader; application is the verified standalone EFI.
[Components]
  UniXNUPkg/Driver/UniXNUDxe/UniXNUDxe.inf
  DarwinBootPkg/Driver/DarwinBoot.inf
'''
(overlay/'Integration.dsc.inc').write_text(inc)
relative='Platforms/Msm8998Pkg/Device/oneplus/cheeseburger/'
changes=[]
for name in ['cheeseburger.dsc','cheeseburger.fdf']:
 f=s/(relative+name);before=f.read_text()
 if name.endswith('dsc'):
  after=before+'\n!include P2Integration/Integration.dsc.inc\n'
 else:
  anchor='    INF ShellPkg/Application/Shell/Shell.inf'
  assert before.count(anchor)==1
  addition='''    # P2: retain the original FD, ARM64 header and memory map.
    FILE APPLICATION = 9B665204-A133-47A2-9396-9E2B370A4E25 {
      SECTION PE32 = P2Integration/UniXNU.efi
      SECTION UI = "UniXNU"
    }
    INF UniXNUPkg/Driver/UniXNUDxe/UniXNUDxe.inf
    INF DarwinBootPkg/Driver/DarwinBoot.inf

'''
  after=before.replace(anchor,addition+anchor)
 f.write_text(after)
 changes.extend(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='a/'+relative+name,tofile='b/'+relative+name))
patch=root/'patches/p2/unixnu-fv.patch';patch.parent.mkdir(parents=True,exist_ok=True);patch.write_text(''.join(changes))
(root/'config/p2/Integration.dsc.inc').write_text(inc)
(root/'out/p2/integration-inputs.json').write_text(json.dumps({'application':str(a.efi.resolve()),'application_sha256':hashlib.sha256(a.efi.read_bytes()).hexdigest(),'source_built':['UniXNUDxe','DarwinBoot'],'binary_embedded':['UniXNU'],'layout':'original cheeseburger FDF; no new BootShim, no memory-map/address change','hardware_validated':False},indent=2)+'\n')
subprocess.run([sys.executable, str(root/'scripts/p2-darwinboot-compat.py'), str(s)], check=True)
print(patch)
