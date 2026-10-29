#!/usr/bin/env python3
"""Add documented phone button semantics to an identity ADT, without touching input drivers."""
from pathlib import Path
import argparse, hashlib, struct, sys
R=Path(__file__).resolve().parents[3];sys.path.insert(0,str(R/'tools/adt'))
from adt import decode,encode
p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
if a.output.exists():raise SystemExit('Refuse to overwrite existing output')
n=decode(a.source.read_bytes());assert not any(c['name']=='buttons' for c in n['children'])
n['children'].append({'name':'buttons','properties':{'name':b'buttons\0','compatible':b'buttons\0','device_type':b'buttons\0','button-names':b'hold\0voldown\0volup\0','home-button-type':struct.pack('<I',2)},'children':[]})
next(c for c in n['children'] if c['name']=='product')['properties']['thin-bezel']=struct.pack('<I',1)
def emit(c):return encode(c['name'],{k:v for k,v in c['properties'].items() if k!='name'},[emit(x) for x in c['children']])
b=emit(n);assert decode(b)==n;a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_bytes(b);print(hashlib.sha256(b).hexdigest())
