#!/usr/bin/env python3
"""Dependency stages with content-hash cache; absent inputs block, never fake success."""
import argparse,hashlib,json,os,subprocess,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def build(config,through):
 graph=json.loads(config.read_text());cache=ROOT/'out/p4/pipeline-cache';cache.mkdir(parents=True,exist_ok=True);results={}
 for name,stage in graph['stages'].items():
  if any(results.get(x,{}).get('status') not in ['PASS','CACHED'] for x in stage.get('depends',[])):
   results[name]={'status':'BLOCKED','reason':'Upstream stage unavailable'}
  else:
   files=[ROOT/x for x in stage.get('inputs',[])];missing=[str(p.relative_to(ROOT)) for p in files if not p.is_file()]
   if missing or not stage.get('command'):results[name]={'status':'BLOCKED','missing':missing,'reason':'Qualified inputs or implementation missing'}
   else:
    key=hashlib.sha256(json.dumps(dict(stage=stage,files={str(p.relative_to(ROOT)):digest(p) for p in files}),sort_keys=True).encode()).hexdigest();prior=cache/(name+'.json');products=[ROOT/x for x in stage['outputs']]
    previous=json.loads(prior.read_text()) if prior.exists() else {}
    if previous.get('key')==key and all(p.is_file() and previous.get('products',{}).get(str(p.relative_to(ROOT)))==digest(p) for p in products):results[name]={'status':'CACHED','key':key}
    else:
     label=name+'-'+str(time.time_ns());cmd=['python3',str(ROOT/'scripts/p4run.py'),label,'--']+stage['command'];v=subprocess.run(cmd,cwd=ROOT)
     if v.returncode or not all(p.is_file() for p in products):results[name]={'status':'FAIL','exit_code':v.returncode,'reason':'Command or output verification failed'}
     else:
      state=dict(key=key,products={str(p.relative_to(ROOT)):digest(p) for p in products});prior.write_text(json.dumps(state,indent=2)+'\n');results[name]={'status':'PASS','key':key}
  if name==through:break
 (ROOT/'out/p4/pipeline-result.json').write_text(json.dumps(results,indent=2)+'\n');print(json.dumps(results,indent=2));return 0 if results.get(through,{}).get('status') in ['PASS','CACHED'] else 78
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--config',type=Path,default=ROOT/'config/p4/pipeline.json');p.add_argument('--through',required=True);a=p.parse_args();raise SystemExit(build(a.config,a.through))
