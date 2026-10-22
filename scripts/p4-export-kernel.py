#!/usr/bin/env python3
"""Promote a genuinely linked standalone ARM64 kernel after Mach-O/tool checks."""
import hashlib,json,shutil,struct,subprocess,argparse
from pathlib import Path
r=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,default=r/'.work/p4-unixnu/build/xnu/ios-obj/DEVELOPMENT_ARM64_MSM8998/mach.development.msm8998.unstripped.noctf');p.add_argument('--output',type=Path,default=r/'out/cheeseburger/kernel');a=p.parse_args();source=a.source.resolve();b=source.read_bytes()
assert r in source.parents and r in a.output.resolve().parents
magic,cpu,subtype,kind,ncmds,sizeofcmds,flags,reserved=struct.unpack_from('<8I',b)
assert (magic,cpu,kind)==(0xfeedfacf,0x100000c,2),'Not ARM64 MH_EXECUTE'
assert 32+sizeofcmds<=len(b)
off=32;segments=[];entry=None
for i in range(ncmds):
 cmd,size=struct.unpack_from('<II',b,off);assert size>=8 and off+size<=32+sizeofcmds
 if cmd==0x19:
  name,va,vs,fo,fs=struct.unpack_from('<16s4Q',b,off+8);assert fo+fs<=len(b) and fs<=vs
  segments.append(dict(name=name.rstrip(b'\0').decode(),vmaddr=va,vmsize=vs,fileoff=fo,filesize=fs))
 if cmd==5:
  flavor,count=struct.unpack_from('<II',b,off+8)
  if flavor==6 and size>=280:entry=struct.unpack_from('<Q',b,off+272)[0]
 off+=size
assert off==32+sizeofcmds and entry is not None,'Missing ARM64 thread entry'
assert any(s['vmaddr']<=entry<s['vmaddr']+s['vmsize'] for s in segments),'Entry outside load segments'
out=a.output.resolve();logs=(r/'out/p4/kernel-validation') if out==r/'out/cheeseburger/kernel' else out/'tool-validation';logs.mkdir(parents=True,exist_ok=True)
checks=[];outputs={}
for label,args in [('file',['file',str(source)]),('lipo',['xcrun','lipo','-info',str(source)]),('load-commands',['xcrun','otool','-l',str(source)]),('symbols',['xcrun','nm','-nm',str(source)]),('undefined',['xcrun','nm','-u',str(source)])]:
 v=subprocess.run(args,capture_output=True,text=True);assert v.returncode==0,label
 (logs/(label+'.log')).write_text(v.stdout+v.stderr);checks.append(dict(command=args,exit_code=v.returncode));outputs[label]=v.stdout
assert not outputs['undefined'].strip(),'Standalone kernel has unresolved symbols'
for symbol in ['__start','_arm_init','_kernel_bootstrap','_pmap_bootstrap','_BootArgs']:
 assert any(line.split() and line.split()[-1]==symbol for line in outputs['symbols'].splitlines()),'Missing '+symbol
assert any(line.startswith(format(entry,'016x')) and line.split()[-1]=='__start' for line in outputs['symbols'].splitlines()),'Thread PC differs from __start symbol'
out.mkdir(parents=True,exist_ok=True);dest=out/'kernel.macho';shutil.copy2(source,dest) if source!=dest else None
report=dict(result='PASS',source=str(source.relative_to(r)),file=str(dest.relative_to(r)),bytes=len(b),sha256=hashlib.sha256(b).hexdigest(),architecture='ARM64',filetype='MH_EXECUTE',entry=hex(entry),segments=segments,checks=checks,undefined_symbols=0,runtime_boot_proven=False)
(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k!='segments'},indent=2))
