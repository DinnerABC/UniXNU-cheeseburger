#!/usr/bin/env python3
"""Map previously identified HID/storage vtable entries to the matching 20H392 donor."""
from pathlib import Path
import re,sys,json,hashlib
R=Path(__file__).resolve().parents[1];stage=R/'out/p23/ios-16.7.16-20H392';sys.path.insert(0,str(stage/'analysis/display-abi/tools'))
from abi import KC
old=KC(str(R/'out/p12/inputs/A/kernelcache.macho'));new=KC(str(stage/'analysis/root-trust-data/kernelcache/kernelcache.thin.macho'))
base=0xfffffff000000000;slide=0xbc0000
report={}
for name,vt,count,ctor,dtor in [('hid',0x6cbe2c0,207,0x5726b9c,0x5726bbc),('ufs',0x6c73f50,190,0x55d9d98,0x55d9db8)]:
 source=R/'package/BoardRuntime'/f'{name}-original-bindings.S';pairs=re.findall(r'^\.set (\S+),\s*(\S+)',source.read_text(),re.M);oldvt=next(int(v,0)+0xc2d000 for k,v in pairs if k.startswith('__ZTV'))
 oldslots=[old.q(oldvt+16+i*8) for i in range(count)];newslots=[new.q(vt+16+i*8) for i in range(count)];rows=[];refs={}
 for symbol,value in pairs:
  if symbol.startswith('__ZTV'):dest=base+vt;origin='vtable'
  elif 'gMetaClass' in symbol:dest=new.q(vt-16);origin='metaclass'
  elif 'C2EPK11OSMetaClass' in symbol:dest=base+ctor;origin='constructor'
  elif 'D2Ev' in symbol:dest=base+dtor;origin='destructor'
  else:
   oldaddr=int(value,0)+0xc2d000;indices=[i for i,x in enumerate(oldslots) if x==oldaddr];targets={newslots[i] for i in indices}
   if not indices or len(targets)!=1 or None in targets:raise SystemExit(f'{name}: ambiguous {symbol}: {indices}')
   dest=targets.pop();origin=indices
  if dest is None or dest<0xfffffff000000000:raise SystemExit(f'bad canonical pointer {symbol}: {dest}')
  refs[symbol]={'source':hex(dest),'indices':origin,'slid':hex(dest-slide)};rows.append(f'.globl {symbol}\n.set {symbol}, {dest-slide:#x}\n')
 path=R/'package/BoardRuntime'/f'{name}-original-bindings-20H392.S';path.write_text(''.join(rows));report[name]={'old_vtable':hex(oldvt),'new_vtable':hex(base+vt),'refs':refs}
(stage/'analysis/display-abi/storage-bindings.json').write_text(json.dumps(report,indent=2)+'\n')
print('HID and storage candidate bindings generated; verify ABI and final slide')
