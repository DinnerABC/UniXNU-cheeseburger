"""One evidence-based full-image retry in a fresh session; NEVER reflash userdata."""
from pathlib import Path
import hashlib,json,sys,time
r=Path(__file__).resolve().parents[1];sys.path.insert(0,str(r/'tools/p13'));from device import Device
o=r/'out/p13/root-retry-clean-session';o.mkdir(exist_ok=True);state=o/'result.json';assert not state.exists(),'No further full-image repeats'
assert json.loads((r/'out/p13/readback.json').read_text())['state']=='DEPLOYED_P12_ROOT_SHA256_PASS'
assert json.loads((r/'out/p13/screen-report-02.json').read_text())['uefi_shell_confirmed']
assert json.loads((r/'out/p13/flash-result.json').read_text())['attempts']==1
old=json.loads((r/'out/p13/boot-result.json').read_text());assert old['attempts']==1 and old['transport_success']
image=r/old['image'];assert hashlib.sha256(image.read_bytes()).hexdigest()==old['image_sha256']
d=Device('clean-root-retry');d.require_root_recovery();assert d.shell('getprop ro.product.device')=='cheeseburger'
rec={'state':'FRESH_BOOTLOADER_REQUESTED','run_dir':str(d.out.relative_to(r)),'image':old['image'],'sha256':old['image_sha256'],'full_root_image_attempt':2,'retry_reason':'The same small FD probe was rejected in the previous warm session but entered EFI Shell after bootloader restart. Retry the full frozen image once after an independently verified Recovery-to-Bootloader transition; no blind repeat.','userdata_writes':False,'partition_writes':False,'fresh_session':True};state.write_text(json.dumps(rec,indent=2)+'\n')
d.run('adb',['reboot','bootloader'])
for _ in range(20):
 try:
  if 'product: msm8998' in d.run('fastboot',['getvar','product'],timeout=2):break
 except RuntimeError:pass
 time.sleep(2)
else:raise RuntimeError('No bootloader transport; STOP')
assert 'unlocked: yes' in d.run('fastboot',['getvar','unlocked']);rec['state']='ONE_FULL_IMAGE_RETRY_STARTED';state.write_text(json.dumps(rec,indent=2)+'\n')
try:d.run('fastboot',['boot',str(image)],timeout=180);rec['state']='BOOT_ACCEPTED_SCREEN_PENDING'
except Exception as e:rec.update(state='FAILED_NO_MORE_ATTEMPTS',error=str(e));raise
finally:state.write_text(json.dumps(rec,indent=2)+'\n')
print(json.dumps(rec,indent=2))
