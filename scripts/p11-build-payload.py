#!/usr/bin/env python3
from pathlib import Path
import json,hashlib,sys
r=Path(__file__).resolve().parents[1];sys.path.insert(0,str(r/'tools/ram_payload'));import bundle;bundle.MAX_BYTES=64*1024*1024
from bundle import pack,manifest
files={};definitions=[]
for i,(name,p) in enumerate([('\\kernel.macho',r/'out/p11/kernel/kernel.macho'),('\\kernel.macho.adt',r/'out/p11/adt/kernel.macho.adt')]):
 b=p.read_bytes();v=json.loads(p.with_name('validation.json').read_text());assert v['result']=='PASS' and hashlib.sha256(b).hexdigest()==v['sha256'];files[name]=b
 definitions.append('STATIC CONST UINT8 P9'+('Kernel' if i==0 else 'Adt')+'Hash[32] = {'+','.join('0x'+hashlib.sha256(b).hexdigest()[j:j+2] for j in range(0,64,2))+'};')
 definitions.append('#define P9'+('Kernel' if i==0 else 'Adt')+'Bytes '+str(len(b)))
config=(r/'config/p11/unixnu.toml').read_bytes();files['\\EFI\\UniXNU\\unixnu.toml']=config
trust=(r/'out/p11/trust/original-static-system.bundle').read_bytes();assert hashlib.sha256(trust).hexdigest()=='7416ca4f8377146626bb284e2133c76bee6cb4e3ee0af5d648b186235a7323a7';files['\\kernel.macho.trustcache']=trust
data=pack(files);o=r/'out/p11/payload';o.mkdir(parents=True,exist_ok=True);(o/'payload.bundle').write_bytes(data)
(o/'manifest.json').write_text(json.dumps(dict(result='PASS',sha256=hashlib.sha256(data).hexdigest(),bytes=len(data),files=manifest(data)),indent=2)+'\n')
(r/'config/p11/Expected.h').write_text('/* Generated from verified P11 inputs. SHA256 checks never bypassed. */\n'+'\n'.join(definitions)+'\n')
print('Actual P11 kernel, ADT and UniXNU XNU entry bundled; hashes bound into firmware diagnostic')
