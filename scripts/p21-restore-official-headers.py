#!/usr/bin/env python3
"""Restore cached official supplementary headers only after Git blob verification."""
from pathlib import Path
import hashlib,json,shutil
R=Path(__file__).resolve().parents[1];old=R/'.work/p4-unixnu/.cache';new=R/'out/p21/official-adaptation/resolved/cheeseburger/ipados-17.7.11-21H461/cheeseburger/tree/.cache';rows=[]
for index,folder in [('xnu-driverkit-10063.121.3.json','iokit/DriverKit'),('xnu-driverkit-crypto-10063.121.3.json','iokit/DriverKit/crypto'),('xnu-trustcache-8796.101.5.json','EXTERNAL_HEADERS/TrustCache')]:
 for e in json.loads((old/'sources'/index).read_text()):
  if e['type']!='file' or not e['name'].endswith('.h'):continue
  data=(old/'xnu-work'/folder/e['name']).read_bytes();blob=hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest();assert blob==e['sha']
  target=new/'xnu-work'/folder/e['name'];target.parent.mkdir(parents=True,exist_ok=True)
  if not target.exists() or target.read_bytes()!=data:target.write_bytes(data)
  rows.append({'path':str(Path(folder)/e['name']),'git_blob':blob,'sha256':hashlib.sha256(data).hexdigest()})
 (new/'sources').mkdir(exist_ok=True);shutil.copy2(old/'sources'/index,new/'sources'/index)
(R/'out/p21/official-adaptation/restored-header-cache.json').write_text(json.dumps({'result':'PINNED_OFFICIAL_HEADER_BLOBS_VERIFIED','headers':rows,'old_kernel_objects_reused':False},indent=2)+'\n');print('Verified supplementary headers',len(rows))
