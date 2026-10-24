#!/usr/bin/env python3
"""Host-only denial/identity tests of the real collector, without a device."""
from pathlib import Path
import importlib.util,json,subprocess,sys,tempfile
from unittest.mock import patch
r=Path(__file__).resolve().parents[1];spec=importlib.util.spec_from_file_location('collector',r/'tools/p9/collect_logs.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
o=r/'out/p9/host-test';o.mkdir(exist_ok=True)
cases=[]
for mode,expected in [('offline',2),('other-device',3),('permission-denied',0)]:
 with tempfile.TemporaryDirectory(prefix='collector-',dir=o) as folder:
  m.ROOT=Path(folder);p=m.ROOT/'out/p3/private';p.mkdir(parents=True);(p/'selected-device.json').write_text(json.dumps(dict(serial='TEST_ONLY')));calls=[]
  def fake(cmd,**kw):
   calls.append(cmd);remote=cmd[3:]
   if remote==['get-state']:rc,data=(1,b'') if mode=='offline' else (0,b'device\n')
   elif remote==['shell','getprop','ro.product.device']:rc,data=0,b'other\n' if mode=='other-device' else b'OnePlus5\n'
   elif remote==['shell','ls','-1','/sys/fs/pstore']:rc,data=0,b'console-ramoops-0\n../../not-allowed\n'
   elif remote[:3]==['shell','-T','cat'] and remote[3].startswith(('/sys/fs/pstore/','/proc/last_kmsg')):rc,data=1,b''
   else:rc,data=0,b'fixture\n'
   return subprocess.CompletedProcess(cmd,rc,data,b'Permission denied' if rc else b'')
  with patch.object(m.subprocess,'run',fake),patch.object(sys,'argv',['collect_logs.py']):rc=m.main()
  assert rc==expected
  assert not any('exec-out' in cmd for cmd in calls)
  files=list((m.ROOT/'out/p9/log-recovery').glob('*/manifest.json'));assert len(files)==1
  result=json.loads(files[0].read_text());assert result['xnu_log_downloaded'] is False
  if mode=='other-device':assert len(calls)==2
  if mode=='permission-denied':
   assert result['nonempty_logs']==0
   assert not any('../../' in str(cmd) for cmd in calls)
   assert not list(files[0].parent.glob('pstore-console-ramoops-0.log'))
   assert (files[0].parent/'pstore-console-ramoops-0.partial').exists()
  cases.append(dict(mode=mode,actual_exit_code=rc,result=result['result']))
(o/'collector-test.json').write_text(json.dumps(dict(result='PASS',cases=cases,hardware_log_collection_proven=False),indent=2)+'\n');print('Offline, wrong-device refusal and denied log reads passed; no fake successful log files')
