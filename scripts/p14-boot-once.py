"""One identity-pinned P14 temporary boot. No partition writes."""
from pathlib import Path
import hashlib,json,sys,time
r=Path(__file__).resolve().parents[1];sys.path.insert(0,str(r/'tools/p14'));from device import Device
o=r/'out/p14';state=o/'boot-result.json';assert not state.exists(),'No duplicate candidate retry'
m=json.loads((o/'uefi/packaging.json').read_text());assert m['result']=='OFFLINE_PACKAGING_PASS'
assert json.loads((o/'candidate-check.json').read_text())['result']=='P14_STATIC_ROOT_AUTH_GATE_PASS'
assert json.loads((o/'logs/pack-compressed-01.json').read_text())['exit_code']==0
fv=json.loads((o/'uefi/compiled-efi-manifest.json').read_text());assert fv['integration_verified'];assert fv['fv_pe_sections']['970879CB-72F0-4876-A365-4BA3E9BA99A0']['sha256']==fv['built_pe_images']['P4RamPayload.efi']['sha256']
p=r/m['image'];assert p.stat().st_size<805306368 and hashlib.sha256(p.read_bytes()).hexdigest()==m['sha256']
assert json.loads((r/'out/p13/readback.json').read_text())['state']=='DEPLOYED_P12_ROOT_SHA256_PASS'
d=Device('root-auth-once');d.require_root_recovery();assert d.shell('getprop ro.product.device')=='cheeseburger'
rec=dict(state='BOOTLOADER_REQUESTED',image=m['image'],bytes=m['bytes'],sha256=m['sha256'],run_dir=str(d.out.relative_to(r)),partition_writes=False,userdata_preserved=True,attempts=1)
state.write_text(json.dumps(rec,indent=2)+'\n');d.run('adb',['reboot','bootloader'])
for _ in range(20):
 try:
  if 'product: msm8998' in d.run('fastboot',['getvar','product'],timeout=2):break
 except RuntimeError:pass
 time.sleep(2)
else:raise RuntimeError('No confirmed bootloader; STOP')
assert 'unlocked: yes' in d.run('fastboot',['getvar','unlocked']);rec['state']='ONE_TEMPORARY_BOOT_STARTED';state.write_text(json.dumps(rec,indent=2)+'\n')
try:d.run('fastboot',['boot',str(p)],timeout=180);rec['state']='TRANSPORT_ACCEPTED_SCREEN_PENDING'
except Exception as e:rec.update(state='FAILED_STOP_NO_RETRY',error=str(e));raise
finally:state.write_text(json.dumps(rec,indent=2)+'\n')
print(json.dumps(rec,indent=2))
