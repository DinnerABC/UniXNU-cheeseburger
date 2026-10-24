#!/usr/bin/env python3
from pathlib import Path
import json,subprocess,hashlib
r=Path(__file__).resolve().parents[1];o=r/'out/p8/native-ufs';o.mkdir(parents=True,exist_ok=True)
rec=json.loads((r/'.work/p4-unixnu/build/xnu/ios-obj/DEVELOPMENT_ARM64_MSM8998/iokit/DEVELOPMENT/IOCPU.cpo.json').read_text().strip().rstrip(','));src=r/'drivers/p8/NativeUfs/NativeUfs.cpp';obj=o/'NativeUfs.o';cmd=[];args=rec['arguments'];i=0
while i<len(args):
 a=args[i]
 if a==rec['file']:cmd.append(str(src))
 elif a=='-o':cmd+=['-o',str(obj)];i+=1
 elif a in ['-MF','-MT','-MJ']:i+=1
 elif a in ['-MMD','-MD','-MP']:pass
 else:cmd.append(a)
 i+=1
cmd+=['-I',str(r/'out/p8/storage-family/include'),'-Wno-error','-Wno-deprecated-declarations']
v=subprocess.run(cmd,cwd=rec['directory'],capture_output=True,text=True);(o/'compile.log').write_text(v.stdout+v.stderr);(o/'build.json').write_text(json.dumps(dict(command=cmd,exit_code=v.returncode,source_sha256=hashlib.sha256(src.read_bytes()).hexdigest(),hardware_tested=False),indent=2)+'\n')
if v.returncode:raise SystemExit(v.returncode)
subprocess.run(['xcrun','ar','rcs',str(o/'libNativeUfs.a'),str(obj)],check=True);print('Actual MSM8998 ARM64 native read-only driver compiled')
