#!/usr/bin/env python3
from pathlib import Path
import plistlib,json,difflib,hashlib
r=Path(__file__).resolve().parents[1];u=r/'.work/p4-unixnu';p=u/'.cache/xnu-work/iokit/KernelConfigTables.cpp';before=p.read_text()
entries=[];report=[]
for name in ['ArmCpu','ArmDTPlatform']:
 f=u/'drivers'/name/'Info.plist';v=plistlib.loads(f.read_bytes());person=next(iter(v['IOKitPersonalities'].values()))
 assert person['IOClass']==name
 fields=[]
 for key in ['IOClass','IOProviderClass','IONameMatch','IOProbeScore']:
  if key in person:
   value=person[key];value=str(value)+':32' if isinstance(value,int) else "'"+value+"'"
   fields.append('    "     '+"'"+key+"' = "+value+';"')
 entries+=['    "   {"',*fields,'    "   },"']
 report.append(dict(driver=name,bundle_id=v['CFBundleIdentifier'],personality=person,source_sha256=hashlib.sha256(f.read_bytes()).hexdigest(),registration='Classes statically linked into kernel; same built-in personality format as pinned upstream, no fictitious loaded kext record'))
if "'IOClass' = 'ArmCpu';" not in before:
 after=before.replace('    "("\n','    "("\n'+'\n'.join(entries)+'\n',1);assert after!=before;p.write_text(after)
else:after=before
if after!=before:(r/'patches/p5/platform-personalities.patch').write_text(''.join(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='a/iokit/KernelConfigTables.cpp',tofile='b/iokit/KernelConfigTables.cpp')))
(r/'out/p5/platform-matching.json').write_text(json.dumps(dict(result='PASS',entries=report,source=str(p.relative_to(r)),runtime_matching_proven=False),indent=2)+'\n')
print('Actual plist-derived built-in ArmCpu and ArmDTPlatform matching installed')
