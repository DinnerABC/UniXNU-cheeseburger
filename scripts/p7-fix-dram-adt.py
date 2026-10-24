#!/usr/bin/env python3
"""Supply required chosen DRAM bounds from the captured contiguous FDT banks.
This is a conservative observed envelope, not a free-memory/allocation map.
"""
from pathlib import Path
import sys,json,struct,hashlib,runpy
r=Path(__file__).resolve().parents[1];sys.path.insert(0,str(r/'tools/adt'));from adt import decode,encode
fdt=r/'out/p3/private/recovery-device.dtb';raw=fdt.read_bytes();observed=json.loads((r/'out/p4/adt-generation.json').read_text());assert hashlib.sha256(raw).hexdigest()==observed['fdt_sha256']
nodes=runpy.run_path(str(r/'scripts/p3-dtb-inspect.py'))['parse'](raw);root=nodes['/'];ac=int.from_bytes(root['#address-cells'],'big');sc=int.from_bytes(root['#size-cells'],'big');assert (ac,sc)==(2,2)
reg=nodes['/memory']['reg'];width=(ac+sc)*4;assert len(reg)%width==0
banks=sorted((int.from_bytes(reg[i:i+ac*4],'big'),int.from_bytes(reg[i+ac*4:i+width],'big')) for i in range(0,len(reg),width));assert banks and all(size>0 for _,size in banks)
for (base,size),(next_base,_) in zip(banks,banks[1:]):assert base+size==next_base,'Refuse to invent a contiguous envelope across a hole'
base=banks[0][0];size=sum(n for _,n in banks)
source=r/'out/p7/milestones/early-pe-cpu-v2/adt/kernel.macho.adt';tree=decode(source.read_bytes());memory=next(n for n in tree['children'] if n['name']=='memory');assert memory['properties']['reg']==b''.join(struct.pack('<QQ',b,n) for b,n in banks)
chosen=next(n for n in tree['children'] if n['name']=='chosen');assert 'dram-base' not in chosen['properties'] and 'dram-size' not in chosen['properties'];assert struct.unpack('<Q',chosen['properties']['darwinboot-memory-size'])[0]==0x20000000
chosen['properties']['dram-base']=struct.pack('<Q',base);chosen['properties']['dram-size']=struct.pack('<Q',size)
def emit(n):return encode(n['name'],n['properties'],[emit(c) for c in n['children']])
data=emit(tree);roundtrip=decode(data);assert roundtrip==tree
o=r/'out/p7/adt';(o/'kernel.macho.adt').write_bytes(data)
v=json.loads((r/'out/p7/milestones/early-pe-cpu-v2/adt/validation.json').read_text());v.update(result='PASS',bytes=len(data),sha256=hashlib.sha256(data).hexdigest(),parent_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),fdt_sha256=hashlib.sha256(raw).hexdigest(),dram_base=hex(base),dram_size=hex(size),banks=[dict(base=hex(b),bytes=n) for b,n in banks],dram_scope='Conservative contiguous envelope of captured FDT /memory/reg, not total installed DRAM capacity or EFI free memory',owned_memory_request_unchanged=0x20000000,runtime_consumed=False)
(o/'validation.json').write_text(json.dumps(v,indent=2)+'\n');print(json.dumps({k:v[k] for k in ['bytes','sha256','dram_base','dram_size','dram_scope','owned_memory_request_unchanged']},indent=2))
