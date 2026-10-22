#!/usr/bin/env python3
"""Render private backup manifests without device identifiers or partition contents."""
import json
from pathlib import Path
r=Path(__file__).resolve().parents[1];rows=[];gpt=[]
for p in sorted((r/'backups/p3').glob('*/manifest.json')):
 for x in json.loads(p.read_text()):
  if x.get('verified'):
   (rows if 'partition' in x else gpt).append(x)
p=r/'docs/P3_BACKUP_MANIFEST.md';text=p.read_text();start=text.index('| Actual partition') if '| Actual partition' in text else text.index('| 实际分区')
end=text.index('## GPT',start)
head='| Actual partition | Block device | Bytes | SHA256 | Project-relative file |\n| --- | --- | ---: | --- | --- |\n'
for x in rows:head+='| '+x['partition']+' | '+x['block_device']+' | '+str(x['actual_bytes'])+' | '+x['sha256']+' | '+x['file']+' |\n'
p.write_text(text[:start]+head+'\n'+text[end:])
p=r/'out/p3/status.json';d=json.loads(p.read_text());d.update(verified_partitions=len(rows),partition_bytes=sum(x['actual_bytes'] for x in rows),verified_gpt_luns=len(gpt),gpt_files=sum(len(x['files']) for x in gpt));p.write_text(json.dumps(d,indent=2)+'\n')
for name in ['README.md','docs/P3_HANDOFF.md']:
 p=r/name;s=p.read_text().replace('41',str(len(rows)));p.write_text(s)
print('Rendered '+str(len(rows))+' verified partitions and '+str(len(gpt))+' GPT LUNs')
