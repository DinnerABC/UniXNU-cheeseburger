"""One controlled warm-state isolation, no partition writes or policy changes."""
from pathlib import Path
import hashlib,json,sys,time
r=Path(__file__).resolve().parents[1];sys.path.insert(0,str(r/'tools/p13'));from device import Device
o=r/'out/p13/reset-session-probe';o.mkdir(exist_ok=True);state=o/'result.json';assert not state.exists(),'No repeated reset/test loop'
prior=json.loads((r/'out/p13/size-probe/boot-result.json').read_text());assert prior['state']=='FAILED_STOP_NO_RETRY'
log=(r/prior['run_dir']/'002.log').read_text();assert '00000050' in log
m=json.loads((r/'out/p13/size-probe/manifest.json').read_text());p=r/m['image'];assert hashlib.sha256(p.read_bytes()).hexdigest()==m['sha256']
d=Device('reset-session');assert 'product: msm8998' in d.run('fastboot',['getvar','product'])
result={'state':'RESET_SESSION_REQUESTED','purpose':'Distinguish stale warm boot state from a repeatable image load/authentication error','baseline':prior,'same_diagnostic_sha256':m['sha256'],'partition_writes':False,'run_dir':str(d.out.relative_to(r))};state.write_text(json.dumps(result,indent=2)+'\n')
d.run('fastboot',['reboot','bootloader'])
for _ in range(20):
 try:
  if 'product: msm8998' in d.run('fastboot',['getvar','product'],timeout=2):break
 except RuntimeError:pass
 time.sleep(2)
else:raise RuntimeError('Bootloader did not return; STOP with no boot attempt')
assert 'unlocked: yes' in d.run('fastboot',['getvar','unlocked'])
result['state']='ONE_DIAGNOSTIC_BOOT_ATTEMPT';state.write_text(json.dumps(result,indent=2)+'\n')
try:d.run('fastboot',['boot',str(p)],timeout=90);result['state']='BOOT_ACCEPTED_SCREEN_PENDING'
except Exception as e:result.update(state='FAILED_STOP_NO_FURTHER_BOOT_ATTEMPTS',error=str(e));raise
finally:state.write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
