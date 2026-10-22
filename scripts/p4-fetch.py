#!/usr/bin/env python3
"""Fetch fixed Apple source inputs directly, entirely within this project."""
import concurrent.futures,json,os,subprocess,tomllib
from pathlib import Path
r=Path(__file__).resolve().parents[1];work=r/'.work/p4-unixnu';up=r.parent/'upstream/UniXNU'
env=dict(os.environ,TMPDIR=str(r/'.work/tmp'),PYTHONDONTWRITEBYTECODE='1')
for k in ['HTTP_PROXY','HTTPS_PROXY','ALL_PROXY','http_proxy','https_proxy','all_proxy']:env.pop(k,None)
def run(cmd):subprocess.run([str(x) for x in cmd],env=env,check=True)
if not work.exists():run(['git','clone','--shared',up,work])
assert subprocess.check_output(['git','-C',str(work),'rev-parse','HEAD'],text=True).strip()==json.loads((r/'sources.lock.json').read_text())['unixnu']['commit']
feeds=tomllib.loads((work/'feeds.toml').read_text())['source'];refs=work/'refer';refs.mkdir(exist_ok=True)
def fetch(s):
 p=refs/s['name']
 if not p.exists():run(['git','clone','--depth','1','--branch',s['ref'],s['git'],p])
 sha=subprocess.check_output(['git','-C',str(p),'rev-parse','HEAD'],text=True).strip()
 if sha!=s['commit']:raise ValueError('Pinned SHA mismatch '+s['name'])
 return dict(name=s['name'],commit=sha,path=str(p.relative_to(r)),repository=s['git'])
selected=[s for s in feeds if s['name'] in ['xnu','libplatform','libdispatch','AvailabilityVersions']]
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:results=list(pool.map(fetch,selected))
(r/'out/p4/sources.json').write_text(json.dumps(results,indent=2)+'\n')
print('All fixed source SHAs verified')
