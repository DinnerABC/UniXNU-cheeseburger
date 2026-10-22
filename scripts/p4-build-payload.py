#!/usr/bin/env python3
from pathlib import Path
import hashlib,json,sys
r=Path(__file__).resolve().parents[1];sys.path.insert(0,str(r/'tools/ram_payload'))
from bundle import pack,manifest
k=r/'out/cheeseburger/kernel/kernel.macho';v=json.loads(k.with_name('validation.json').read_text())
assert v['result']=='PASS' and hashlib.sha256(k.read_bytes()).hexdigest()==v['sha256']
a=r/'out/cheeseburger/adt/kernel.macho.adt';check=json.loads((r/'out/p4/adt-parser/validation.json').read_text())
assert check['exit_code']==0 and hashlib.sha256(a.read_bytes()).hexdigest()==check['adt_sha256']
files={'\\EFI\\UniXNU\\unixnu.toml':(r/'config/p4/ram-probe.toml').read_bytes(),'\\kernel.macho':k.read_bytes(),'\\kernel.macho.adt':a.read_bytes()}
data=pack(files);o=r/'out/cheeseburger/payload';o.mkdir(parents=True,exist_ok=True);(o/'ram-probe.bundle').write_bytes(data)
(o/'manifest.json').write_text(json.dumps(dict(result='PASS',bytes=len(data),sha256=hashlib.sha256(data).hexdigest(),files=manifest(data),boot_policy='probe only; return to firmware; XNU not entered'),indent=2)+'\n')
print('Read-only RAM bundle verified:',len(data),'bytes; XNU auto-entry disabled')
