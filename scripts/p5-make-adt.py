#!/usr/bin/env python3
from pathlib import Path
import sys,struct,zlib,ast,json,hashlib
r=Path(__file__).resolve().parents[1];sys.path.insert(0,str(r/'tools/adt'));from adt import decode,encode
src=r/'.work/p4-unixnu/scripts/qemu-entry/make-dt.py';text=src.read_text();module=ast.parse(text);func=next(x for x in module.body if isinstance(x,ast.FunctionDef) and x.name=='empty_nvram_proxy');ns={'struct':struct,'zlib':zlib};exec(compile(ast.Module(body=[func],type_ignores=[]),str(src),'exec'),ns)
base=r/'out/cheeseburger/adt/kernel.macho.adt';tree=decode(base.read_bytes());chosen=next(x for x in tree['children'] if x['name']=='chosen');proxy=dict(ns['empty_nvram_proxy']());chosen['properties'].update(proxy)
chosen['properties']['unixnu,nvram-policy']=b'volatile-software-only\0'
image=proxy['nvram-proxy-data'];assert len(image)==struct.unpack('<I',proxy['nvram-bank-size'])[0]==8192
assert struct.unpack_from('<I',image,16)[0]==zlib.adler32(image[20:])
at=0;parts=[]
while at<len(image):
 sig,check,blocks,name=struct.unpack_from('<BBH12s',image,at);size=blocks*16;assert sig==0x70 and size>=16 and at+size<=len(image)
 raw=image[at:at+16];total=raw[0]+sum(raw[2:]);
 while total>255:total=(total&255)+(total>>8)
 assert check==total;parts.append(dict(name=name.rstrip(b'\0').decode(),offset=at,bytes=size));at+=size
assert parts[1]['name']=='common' and at==8192
cpus=next(x for x in tree['children'] if x['name']=='cpus');assert len(cpus['children'])==8
assert sum(x['properties']['state']==b'running\0' for x in cpus['children'])==1
bus=next(x for x in tree['children'] if x['name']=='arm-io');timer=next(x for x in bus['children'] if x['name']=='timer');assert struct.unpack('<I',timer['properties']['interrupts'])[0]==19

def emit(n):return encode(n['name'],n['properties'],[emit(c) for c in n['children']])
data=emit(tree);decode(data);o=r/'out/p5/adt';o.mkdir(parents=True,exist_ok=True);(o/'kernel.macho.adt').write_bytes(data)
(o/'validation.json').write_text(json.dumps(dict(result='PASS',bytes=len(data),sha256=hashlib.sha256(data).hexdigest(),base_sha256=hashlib.sha256(base.read_bytes()).hexdigest(),proxy_source_sha256=hashlib.sha256(src.read_bytes()).hexdigest(),nvram_policy='Empty volatile software CHRP bank, no persistence controller or real Apple NVRAM',partitions=parts,timer_intid=19,cpu_count=8,boot_cpu_affinity=0,runtime_consumed=False),indent=2)+'\n');print('ADT validated: real hardware retained; empty volatile CHRP proxy added')
