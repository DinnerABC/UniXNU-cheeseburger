#!/usr/bin/env python3
from pathlib import Path
import subprocess,json,tomllib
r=Path(__file__).resolve().parents[1];u=r/'.work/p4-unixnu';s=next(x for x in tomllib.loads((u/'feeds.toml').read_text())['source'] if x['name']=='corecrypto-2023');p=u/'refer'/s['name']
def run(*cmd):subprocess.run([str(x) for x in cmd],check=True)
if not p.exists():
 run('git','init',p);run('git','-C',p,'remote','add','origin',s['git']);run('git','-C',p,'fetch','--depth','1','origin',s['commit']);run('git','-C',p,'checkout','--detach','FETCH_HEAD')
sha=subprocess.check_output(['git','-C',p,'rev-parse','HEAD'],text=True).strip();assert sha==s['commit']
(r/'out/p4/corecrypto-source.json').write_text(json.dumps(dict(repository=s['git'],commit=sha,provenance='Pinned upstream third-party snapshot; not authenticated against Apple archive'),indent=2)+'\n')
