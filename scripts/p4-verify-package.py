#!/usr/bin/env python3
from pathlib import Path
import json,hashlib,struct
r=Path(__file__).resolve().parents[1];v=json.loads((r/'out/p2/unixnu-package/result.json').read_text());checks={}
assert v['result']=='PASS'
for rel,m in v['products'].items():
 p=r/rel;b=p.read_bytes();assert len(b)==m['size'] and hashlib.sha256(b).hexdigest()==m['sha256']
 off=struct.unpack_from('<I',b,60)[0];assert b[:2]==b'MZ' and b[off:off+4]==b'PE\0\0' and struct.unpack_from('<H',b,off+4)[0]==0xaa64
 checks[rel]=m
(r/'out/p4/package-verification.json').write_text(json.dumps(dict(result='PASS',mode='reuse verified P2 source-built package',products=checks),indent=2)+'\n');print('Existing P2 AArch64 package revalidated')
