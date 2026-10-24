#!/usr/bin/env python3
"""Fetch only small official AOSP source files directly, no firmware or proxies."""
from pathlib import Path
import base64,hashlib,json,urllib.request
r=Path(__file__).resolve().parents[1];o=r/'out/p13/fastboot-source';o.mkdir(exist_ok=True);openurl=urllib.request.build_opener(urllib.request.ProxyHandler({}));records=[]
base='https://android.googlesource.com/platform/system/core/+/refs/tags/platform-tools-35.0.2/'
for name in ['fastboot/fastboot.cpp','libsparse/sparse_read.cpp','libsparse/sparse.cpp','libsparse/include/sparse/sparse.h']:
 url=base+name+'?format=TEXT'
 try:
  with openurl.open(url,timeout=30) as response:encoded=response.read(2*1024*1024+1)
  assert len(encoded)<=2*1024*1024;data=base64.b64decode(encoded);p=o/name;p.parent.mkdir(exist_ok=True,parents=True);p.write_bytes(data);records.append({'url':url,'file':str(p.relative_to(r)),'sha256':hashlib.sha256(data).hexdigest(),'bytes':len(data)})
 except Exception as e:records.append({'url':url,'error':str(e)});raise
 finally:(o/'sources.json').write_text(json.dumps(records,indent=2)+'\n')
print('Fixed Platform Tools 35.0.2 AOSP source fetched directly')
