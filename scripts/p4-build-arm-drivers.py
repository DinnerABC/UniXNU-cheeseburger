#!/usr/bin/env python3
"""Build actual generic driver sources with the verified MSM8998 XNU C++ ABI recipe."""
from pathlib import Path
import json,subprocess,hashlib,struct
r=Path(__file__).resolve().parents[1];u=r/'.work/p4-unixnu';base=u/'build/xnu/ios-obj/DEVELOPMENT_ARM64_MSM8998'
recipe=base/'iokit/DEVELOPMENT/IOCPU.cpo.json';rec=json.loads(recipe.read_text().strip().rstrip(','))
o=r/'out/p4/arm-drivers';o.mkdir(parents=True,exist_ok=True);report={}
for name in ['ArmCpu','ArmDTPlatform']:
 s=u/'drivers'/name/(name+'.cpp');obj=o/(name+'.o');cmd=[];args=rec['arguments'];i=0
 while i<len(args):
  v=args[i]
  if v==rec['file']:cmd.append(str(s))
  elif v=='-o':cmd.extend(['-o',str(obj)]);i+=1
  elif v in ['-MF','-MT','-MJ']:i+=1
  elif v in ['-MMD','-MD','-MP']:pass
  else:cmd.append(v)
  i+=1
 v=subprocess.run(cmd,cwd=rec['directory'],capture_output=True,text=True);(o/(name+'.log')).write_text(v.stdout+v.stderr)
 report[name]=dict(command=cmd,exit_code=v.returncode,source_sha256=hashlib.sha256(s.read_bytes()).hexdigest(),runtime_validated=False)
 if v.returncode:(o/'result.json').write_text(json.dumps(report,indent=2)+'\n');raise SystemExit(v.returncode)
 b=obj.read_bytes();assert struct.unpack_from('<II',b)==(0xfeedfacf,0x100000c)
 archive=o/('lib'+name+'.a');subprocess.run(['xcrun','ar','rcs',str(archive),str(obj)],check=True)
 report[name].update(archive=str(archive.relative_to(r)),sha256=hashlib.sha256(archive.read_bytes()).hexdigest(),architecture='ARM64',kernel_linked=False)
(o/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('Actual ArmCpu (ADT timer PPI) and ArmDTPlatform ARM64 archives compiled; not kernel linked or runtime proven')
