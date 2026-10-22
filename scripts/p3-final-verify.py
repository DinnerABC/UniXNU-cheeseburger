#!/usr/bin/env python3
"""Offline integrity check of actual P3 backups, images and code; no device writes."""
import hashlib,json,subprocess
from pathlib import Path
r=Path(__file__).resolve().parents[1]
def sha(path):
 h=hashlib.sha256()
 with path.open('rb') as f:
  for chunk in iter(lambda:f.read(1048576),b''):h.update(chunk)
 return h.hexdigest()
partitions=0;gpt_files=0
for manifest in sorted((r/'backups/p3').glob('*/manifest.json')):
 for rec in json.loads(manifest.read_text()):
  assert rec['verified'],manifest
  if 'partition' in rec:
   f=r/rec['file'];assert f.stat().st_size==rec['expected_bytes']==rec['actual_bytes']
   assert sha(f)==rec['sha256']==rec['device_sha256_before']==rec['device_sha256_after']
   assert rec['exit_code']==0;partitions+=1
  else:
   for item in rec['files']:
    f=r/item['file'];assert f.stat().st_size==item['bytes'] and sha(f)==item['sha256'];gpt_files+=1
images={'out/p2/baseline/boot-cheeseburger.img':'c52e0ab0100a8bfb46c6d66c888455147d28634b6b88ab1d93599d7b87ca6433','out/p2/integrated/boot-cheeseburger.img':'dc599569ca8a9edd5f1d6c3a6edf37bc61e8e5b1636220b4b3c00e73a87d12e8','out/p3/diagnostic/boot-cheeseburger.img':'84cbf7983ee9a24ac8658dd1ec645a2b0d5f7cb0cd9eefa5bed97eb4b700b4d5'}
for name,h in images.items():assert sha(r/name)==h,name
for f in (r/'scripts').glob('p3*.py'):compile(f.read_text(),str(f),'exec')
subprocess.run(['bash','-n',str(r/'scripts/p3-env.sh')],check=True)
subprocess.run(['git','check-ignore','-q','backups/p3/20261022-023530/persist.img'],cwd=r,check=True)
assert partitions==58 and gpt_files==12
report=dict(result='PASS',verified_partitions=partitions,verified_gpt_files=gpt_files,images=images,p3_python_syntax='PASS',shell_syntax='PASS',backups_git_ignored=True)
(r/'out/p3/final-integrity.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS: 58 partition files, 12 GPT files, 3 boot images and P3 script syntax')
