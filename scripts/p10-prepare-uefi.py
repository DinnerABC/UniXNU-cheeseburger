#!/usr/bin/env python3
from pathlib import Path
import difflib,hashlib,json,shutil,re
r=Path(__file__).resolve().parents[1];s=Path('/home/dinnerabc.guest/unixnu-p9/source');assert (s/'.p10-overlay-marker').exists()
changes=[]
def write(rel,data):
 p=s/rel;old=p.read_text();p.write_text(data);changes.extend(difflib.unified_diff(old.splitlines(True),data.splitlines(True),fromfile='a/'+rel,tofile='b/'+rel))
for rel in ['P9Integration/Probe/P9Expected.h','DarwinBootPkg/Driver/P9Expected.h']:write(rel,(r/'config/p10/Expected.h').read_text())
for rel in ['P9Integration/Probe/P9Probe.c','DarwinBootPkg/Driver/DarwinBoot.c']:
 p=s/rel;write(rel,p.read_text().replace('L"P9 ', 'L"P10 '))
base=s/'Platforms/Msm8998Pkg/Device/oneplus/cheeseburger'
active=[]
for file in ['cheeseburger.dsc','cheeseburger.fdf']:
 matches=re.findall(r'([A-Za-z0-9_/.-]+/RamPayload/P4RamPayload\.inf)',(base/file).read_text());assert len(matches)==1
 active.append(matches[0])
assert active[0]==active[1]
driver_header=str(Path(active[0]).parent/'Range.h')
for rel in [driver_header,'Platforms/Msm8998FamilyPkg/MemoryInitPei/P4Range.h']:
 write(rel,(r/'config/p10/RamPayload/Range.h').read_text())
(r/'patches/p10/uefi-incremental.patch').write_text(''.join(changes))
o=r/'out/p10/uefi-source';o.mkdir(exist_ok=True)
for rel in ['P9Integration/Probe/P9Probe.c','P9Integration/Probe/P9Expected.h','DarwinBootPkg/Driver/DarwinBoot.c',driver_header,'Platforms/Msm8998FamilyPkg/MemoryInitPei/P4Range.h']:
 p=s/rel;dest=o/rel;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,dest)
(o/'manifest.json').write_text(json.dumps({str(p.relative_to(o)):hashlib.sha256(p.read_bytes()).hexdigest() for p in o.rglob('*') if p.is_file() and p.name!='manifest.json'},indent=2)+'\n')
(o/'active-ram-driver.json').write_text(json.dumps(dict(inf=active[0],header=driver_header,maximum_payload_bytes=67108864,sha256=hashlib.sha256((s/driver_header).read_bytes()).hexdigest(),selected_by_both_dsc_and_fdf=True),indent=2)+'\n')
print('P10 private source overlay: hash gates updated; 64 MiB protocol cap retains FDT, conventional-memory, overlap and reserved-HOB checks')
