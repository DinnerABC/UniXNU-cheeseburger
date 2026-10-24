"""Single new compressed candidate; no partition writes and no automatic retry."""
from pathlib import Path
import json,hashlib,sys,time
r=Path(__file__).resolve().parents[1];sys.path.insert(0,str(r/'tools/p13'));from device import Device
o=r/'out/p13/compressed';state=o/'boot-result.json';assert not state.exists(),'Already attempted; no duplicate boot'
m=json.loads((o/'uefi/packaging.json').read_text());assert m['result']=='OFFLINE_PACKAGING_PASS'
assert json.loads((r/'out/p13/logs/pack-compressed-01.json').read_text())['exit_code']==0
fv=json.loads((o/'uefi/compiled-efi-manifest.json').read_text());assert fv['integration_verified'];assert fv['fv_pe_sections']['970879CB-72F0-4876-A365-4BA3E9BA99A0']['sha256']==fv['built_pe_images']['P4RamPayload.efi']['sha256']
p=r/m['image'];assert hashlib.sha256(p.read_bytes()).hexdigest()==m['sha256'];assert p.stat().st_size<805306368
assert json.loads((r/'out/p13/readback.json').read_text())['state']=='DEPLOYED_P12_ROOT_SHA256_PASS'
d=Device('compressed-once');assert 'product: msm8998' in d.run('fastboot',['getvar','product']);assert 'unlocked: yes' in d.run('fastboot',['getvar','unlocked'])
rec=dict(state='FRESH_SESSION_REQUESTED',image=m['image'],bytes=m['bytes'],sha256=m['sha256'],run_dir=str(d.out.relative_to(r)),partition_writes=False,attempts=1,reason='New lossless compressed payload and verified UEFI decoder; original full raw image not repeated')
state.write_text(json.dumps(rec,indent=2)+'\n');d.run('fastboot',['reboot','bootloader'])
for _ in range(20):
 try:
  if 'product: msm8998' in d.run('fastboot',['getvar','product'],timeout=2):break
 except RuntimeError:pass
 time.sleep(2)
else:raise RuntimeError('Bootloader unavailable; STOP')
assert 'unlocked: yes' in d.run('fastboot',['getvar','unlocked']);rec['state']='ONE_TEMPORARY_BOOT_STARTED';state.write_text(json.dumps(rec,indent=2)+'\n')
try:d.run('fastboot',['boot',str(p)],timeout=180);rec['state']='TRANSPORT_ACCEPTED_EXECUTION_UNCONFIRMED'
except Exception as e:rec.update(state='FAILED_STOP_NO_RETRY',error=str(e));raise
finally:state.write_text(json.dumps(rec,indent=2)+'\n')
print(json.dumps(rec,indent=2))
