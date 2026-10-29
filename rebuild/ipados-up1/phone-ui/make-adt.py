#!/usr/bin/env python3
"""Create a separate phone-identity ADT from the retained iPadOS candidate."""
from pathlib import Path
import hashlib,json,sys
R=Path(__file__).resolve().parents[3];sys.path.insert(0,str(R/'tools/adt'))
from adt import decode,encode
source=R/'out/p21/touch-window2g/adt/kernel.macho.adt'
donor=R/'out/p23/ios-16.7.16-20H392/adt/kernel.macho.adt'
out=R/'out/p21/phone-ui/adt/kernel.macho.adt'
if out.exists():raise SystemExit('Output exists; preserve previous candidate')
n=decode(source.read_bytes());d=decode(donor.read_bytes())
p=next(c for c in n['children'] if c['name']=='product');q=next(c for c in d['children'] if c['name']=='product')
for k in ('model','target-type','target-sub-type'):n['properties'][k]=d['properties'][k]
for k in ('product-name','product-description','artwork-device-idiom','artwork-device-subtype','artwork-scale-factor','artwork-display-gamut','compatible-device-fallback'):p['properties'][k]=q['properties'][k]
def emit(c):return encode(c['name'],{k:v for k,v in c['properties'].items() if k!='name'},[emit(x) for x in c['children']])
b=emit(n);assert decode(b)==n;out.parent.mkdir(parents=True,exist_ok=True);out.write_bytes(b)
print(json.dumps({'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'donor_sha256':hashlib.sha256(donor.read_bytes()).hexdigest(),'output_sha256':hashlib.sha256(b).hexdigest(),'ui':'phone','os':'iPadOS 17.7.11','compatible_unchanged':True},indent=2))
