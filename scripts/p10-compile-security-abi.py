#!/usr/bin/env python3
"""Compile field offsets against the actual kernel headers/compile recipe."""
from pathlib import Path
import json,struct,subprocess,sys
r=Path(__file__).resolve().parents[1];sys.path.insert(0,str(r/'.work/p4-unixnu/kits/NativePrelink'));import native_prelink as np
o=r/'out/p10/security-abi';o.mkdir(exist_ok=True)
fields=dict(amfi_size='sizeof(amfi_t)',tc_offset='__builtin_offsetof(amfi_t,TrustCache)',tc_size='sizeof(TrustCacheInterface_t)',tc_loadModule='__builtin_offsetof(TrustCacheInterface_t,loadModule)',tc_load='__builtin_offsetof(TrustCacheInterface_t,load)',tc_query='__builtin_offsetof(TrustCacheInterface_t,query)',img4_size='sizeof(img4_interface_t)',img4_expected_version='IMG4_INTERFACE_VERSION',img4_runtime_default='__builtin_offsetof(img4_interface_t,i4if_v7.runtime_default)',kmod_size='sizeof(kmod_info_t)',kmod_start='__builtin_offsetof(kmod_info_t,start)',kmod_stop='__builtin_offsetof(kmod_info_t,stop)')
src=o/'layout.c';src.write_text('#include <libkern/libkern.h>\n#include <libkern/amfi/amfi.h>\n#include <libkern/img4/interface.h>\n#include <mach/kmod.h>\n__attribute__((used)) const unsigned long p10_security_abi[] = {'+','.join(fields.values())+'};\n')
rec=json.loads((r/'.work/p4-unixnu/build/xnu/ios-obj/DEVELOPMENT_ARM64_MSM8998/libkern/DEVELOPMENT/amfi.o.json').read_text().strip().rstrip(','));cmd=[];args=rec['arguments'];i=0;obj=o/'layout.o'
while i<len(args):
 a=args[i]
 if a==rec['file']:cmd.append(str(src))
 elif a=='-o':cmd+=['-o',str(obj)];i+=1
 elif a in ['-MF','-MT','-MJ']:i+=1
 elif a in ['-MMD','-MD','-MP']:pass
 else:cmd.append(a)
 i+=1
cmd+=['-Wno-error'];v=subprocess.run(cmd,cwd=rec['directory'],capture_output=True,text=True);(o/'compile.log').write_text(v.stdout+v.stderr);(o/'command.json').write_text(json.dumps(dict(command=cmd,exit_code=v.returncode),indent=2)+'\n')
if v.returncode:raise SystemExit(v.returncode)
b=obj.read_bytes();m=np.macho(b);sym=next(a for a,names in np.exports(b,m).items() if '_p10_security_abi' in names);at=np.backed_offset(m['segments'],sym,len(fields)*8,len(b));values=struct.unpack_from('<'+str(len(fields))+'Q',b,at);report=dict(zip(fields,values));(o/'layout.json').write_text(json.dumps(report,indent=2)+'\n');print(report)
