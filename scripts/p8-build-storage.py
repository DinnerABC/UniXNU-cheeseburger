#!/usr/bin/env python3
"""Compile real public IOStorageFamily with this kernel's actual C++ ABI flags."""
from pathlib import Path
import json,subprocess,shutil,hashlib,sys
r=Path(__file__).resolve().parents[1];s=r/'upstream/IOStorageFamily';o=r/'out/p8/storage-family';o.mkdir(parents=True,exist_ok=True);inc=o/'include';headers=inc/'IOKit/storage';headers.mkdir(parents=True,exist_ok=True)
for f in s.glob('*.h'):shutil.copy2(f,headers/f.name)
sdk=Path(subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip());shutil.copy2(sdk/'usr/include/TargetConditionals.h',inc/'TargetConditionals.h')
rec=json.loads((r/'.work/p4-unixnu/build/xnu/ios-obj/DEVELOPMENT_ARM64_MSM8998/iokit/DEVELOPMENT/IOCPU.cpo.json').read_text().rstrip(',\n'));report=[];objects=[]
# DriverKit user-server glue is a separate optional service, not used by native devices.
files=['IOStorage','IOMedia','IOBlockStorageDevice','IOBlockStorageDriver','IOPartitionScheme','IOGUIDPartitionScheme','IOFDiskPartitionScheme','IOFilterScheme','IOMediaBSDClient']
adapt=o/'source';adapt.mkdir(exist_ok=True)
for f in s.glob('*.h'):shutil.copy2(f,adapt/f.name)
for name in files:
 text=(s/(name+'.cpp')).read_text()
 if name=='IOBlockStorageDriver':
  # Native kernel providers retain the original native perf-control path.
  # DriverKit user-server classes require generated IIG RPC headers and are
  # not part of this kernel-only target. No virtual block service is faked.
  text=text.replace('#include "IOUserBlockStorageDevice_kext.h"','// P8: native kernel block providers only')
  start=text.index('    _userBlockStorageDevice = OSDynamicCast(IOUserBlockStorageDevice, provider);')
  end=text.index('        _perfControlClient = IOPerfControlClient::copyClient(this, 0);',start)
  text=text[:start]+'    _userBlockStorageDevice = NULL;\n    {\n'+text[end:]
 (adapt/(name+'.cpp')).write_text(text)
for name in files:
 f=adapt/(name+'.cpp');obj=o/(name+'.o');args=rec['arguments'];cmd=[];i=0
 while i<len(args):
  a=args[i]
  if a==rec['file']:cmd.append(str(f))
  elif a=='-o':cmd+=['-o',str(obj)];i+=1
  elif a in ['-MF','-MT','-MJ']:i+=1
  elif a in ['-MMD','-MD','-MP']:pass
  else:cmd.append(a)
  i+=1
 cmd+=['-I',str(inc),'-I',str(s),'-Wno-error','-Wno-deprecated-declarations','-Wno-register']
 v=subprocess.run(cmd,cwd=rec['directory'],capture_output=True,text=True);(o/(name+'.log')).write_text(v.stdout+v.stderr);report.append(dict(source=str(f.relative_to(r)),source_sha256=hashlib.sha256(f.read_bytes()).hexdigest(),command=cmd,exit_code=v.returncode));(o/'build.json').write_text(json.dumps(report,indent=2)+'\n');print(name,v.returncode,flush=True)
 if v.returncode:raise SystemExit(v.returncode)
 objects.append(str(obj))
subprocess.run(['xcrun','ar','rcs',str(o/'libIOStorageFamily.a')]+objects,check=True)
print('Real ARM64 IOStorageFamily archive built, including GPT and BSD media client')
