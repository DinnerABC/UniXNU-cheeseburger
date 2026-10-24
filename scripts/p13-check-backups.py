from pathlib import Path
import hashlib,json
r=Path(__file__).resolve().parents[1];required={'persist','modemst1','modemst2','fsg','fsc','boot','recovery','abl','xbl','tz','rpm','hyp','devcfg'};checked=[]
def sha(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  while b:=f.read(8<<20):h.update(b)
 return h.hexdigest()
for m in (r/'backups/p3').glob('*/manifest.json'):
 rows=json.loads(m.read_text())
 if not isinstance(rows,list):continue
 for row in rows:
  if row.get('partition') in required:
   p=r/row['file'];assert row['verified'] and row['exit_code']==0 and p.stat().st_size==row['expected_bytes']==row['actual_bytes'] and sha(p)==row['sha256'];checked.append({'partition':row['partition'],'file':row['file'],'bytes':p.stat().st_size,'sha256':row['sha256']})
assert required<={x['partition'] for x in checked}
gpt=json.loads((r/'backups/p3/gpt-20261022-023658/manifest.json').read_text());gptchecked=[]
for rec in gpt:
 assert rec['verified']
 for f in rec['files']:
  p=r/f['file'];assert p.stat().st_size==f['bytes'] and sha(p)==f['sha256'];gptchecked.append(f['file'])
result={'result':'P3_LOCAL_BACKUP_SHA256_PASS','critical_backups':checked,'gpt_files':gptchecked,'actual_restore_executed':False};(r/'out/p13/backup-check.json').write_text(json.dumps(result,indent=2)+'\n');print('Critical local backup files and all GPT backup files pass SHA256')
