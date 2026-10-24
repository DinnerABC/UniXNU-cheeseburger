#!/usr/bin/env python3
from pathlib import Path
import json,hashlib,sys
r=Path(__file__).resolve().parents[1];sys.path.insert(0,str(r/'tools/ram_payload'));from bundle import pack,manifest
files={};definitions=[]
for i,(name,p) in enumerate([('\\kernel.macho',r/'out/p8/kernel/kernel.macho'),('\\kernel.macho.adt',r/'out/p8/adt/kernel.macho.adt')]):
 b=p.read_bytes();v=json.loads(p.with_name('validation.json').read_text());assert v['result']=='PASS' and hashlib.sha256(b).hexdigest()==v['sha256'];files[name]=b
 definitions.append('STATIC CONST UINT8 P8'+('Kernel' if i==0 else 'Adt')+'Hash[32] = {'+','.join('0x'+hashlib.sha256(b).hexdigest()[j:j+2] for j in range(0,64,2))+'};')
 definitions.append('#define P8'+('Kernel' if i==0 else 'Adt')+'Bytes '+str(len(b)))
config=(r/'config/p8/unixnu.toml').read_bytes();files['\\EFI\\UniXNU\\unixnu.toml']=config
data=pack(files);o=r/'out/p8/payload';o.mkdir(parents=True,exist_ok=True);(o/'payload.bundle').write_bytes(data)
(o/'manifest.json').write_text(json.dumps(dict(result='PASS',sha256=hashlib.sha256(data).hexdigest(),bytes=len(data),files=manifest(data)),indent=2)+'\n')
(r/'config/p8/Expected.h').write_text('/* Generated from verified P8 inputs. SHA256 checks never bypassed. */\n'+'\n'.join(definitions)+'\n')
print('Actual P8 kernel, ADT and UniXNU XNU entry bundled; hashes bound into firmware diagnostic')
