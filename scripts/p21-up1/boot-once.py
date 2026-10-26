"""Exactly one identity-pinned P16 fastboot boot after offline gates. Only the user-authorized persistent log window may be written by XNU."""
from pathlib import Path
import hashlib,json,sys,time,re
r=Path(__file__).resolve().parents[2];sys.path.insert(0,str(r/'scripts/p21-up1'));from device import Device
o=r/'out/p21/upstream-a';state=o/'boot-result.json';assert not state.exists(),'No duplicate candidate retry'
gate=json.loads((o/'offline-gate.json').read_text());assert gate['result']=='P21_UP1_BOUNDED_PERSIST_LOG_BOOT_INPUT_PASS'
m=json.loads((o/'uefi/packaging.json').read_text());assert m['result']=='OFFLINE_PACKAGING_PASS';p=r/m['image'];assert p.stat().st_size==gate['image_bytes'] and hashlib.sha256(p.read_bytes()).hexdigest()==m['sha256']==gate['image_sha256']
assert json.loads((r/'out/p13/readback.json').read_text())['state']=='DEPLOYED_P12_ROOT_SHA256_PASS'
d=Device('p21-upstream-a-persistlog-once');in_recovery=False
try:
 d.require_root_recovery();assert d.shell('getprop ro.product.device')=='cheeseburger';in_recovery=True
except RuntimeError:
 # A pre-existing bootloader is allowed only for the same serial-pinned device whose cheeseburger identity was recorded.
 assert 'product: msm8998' in d.run('fastboot',['getvar','product'],timeout=3)
rec=dict(state='IDENTITY_CHECKED_BOOTLOADER_PENDING',image=m['image'],bytes=m['bytes'],sha256=m['sha256'],run_dir=str(d.out.relative_to(r)),partition_writes_authorized=True,fastboot_flash_commands=0,runtime_log_writes="pending recovery verification",system_regions_protected=True,log_window="out/p21/upstream-a/log-window.json",attempts=0,boot_commands=0,expected_owner_sha384=gate['expected_owner_sha384'])
with state.open('x') as receipt:receipt.write(json.dumps(rec,indent=2)+'\n')
if in_recovery:
 assert '/dev/block/sda14 ' not in d.shell('cat /proc/mounts'), 'userdata must be unmounted'
 d.run('adb',['reboot','bootloader'])
for _ in range(20):
 try:
  if 'product: msm8998' in d.run('fastboot',['getvar','product'],timeout=2):break
 except RuntimeError:pass
 time.sleep(2)
else:raise RuntimeError('No confirmed bootloader; STOP')
assert 'unlocked: yes' in d.run('fastboot',['getvar','unlocked'])
limit_reply=d.run('fastboot',['getvar','max-download-size']);limit_match=re.search(r'max-download-size:\s*(0x[0-9a-fA-F]+|[0-9]+)',limit_reply)
assert limit_match,'Bootloader did not report max-download-size; stop'
limit=int(limit_match.group(1),0);assert len(p.read_bytes())<=limit,'Image exceeds actual bootloader limit'
assert m['sha256']!=json.loads((r/'out/p17/boot-result.json').read_text())['sha256'],'Known tested SHA must not be resent'
assert m['sha256']!=json.loads((r/'out/p17/r2/boot-result.json').read_text())['sha256'],'Known R2 tested SHA must not be resent'
assert m['sha256']!=json.loads((r/'out/p17/r3/boot-result.json').read_text())['sha256'],'Known R3 tested SHA must not be resent'
assert m['sha256']!=json.loads((r/'out/p17/r4/boot-result.json').read_text())['sha256'],'Known R4 SHA must not be resent'
assert m['sha256']!=json.loads((r/'out/p18/boot-result.json').read_text())['sha256'],'Known P18 SHA must not be resent'
assert m['sha256']!=json.loads((r/'out/p19/boot-result.json').read_text())['sha256'],'Known P19 diagnostic SHA must not be resent'
assert m['sha256']!=json.loads((r/'out/p19/r2/boot-result.json').read_text())['sha256'], 'P19-R2 SHA must never be resent'
assert m['sha256']!=json.loads((r/'out/p20/boot-result.json').read_text())['sha256'], 'P20-R1 SHA must never be resent'
assert m['sha256']!=json.loads((r/'out/p20/r2/boot-result.json').read_text())['sha256']
assert m['sha256']!=json.loads((r/'out/p20/r3/boot-result.json').read_text())['sha256']
assert m['sha256']!=json.loads((r/'out/p20/r4/boot-result.json').read_text())['sha256']
assert m['sha256']!=json.loads((r/'out/p20/r5/boot-result.json').read_text())['sha256']
assert m['sha256']!=json.loads((r/'out/p20/r6/boot-result.json').read_text())['sha256']
assert m['sha256']!=json.loads((r/'out/p20/r7/boot-result.json').read_text())['sha256']
assert m['sha256']!=json.loads((r/'out/p20/r8/uefi/packaging.json').read_text())['sha256']
assert m['sha256']!=json.loads((r/'out/p20/r9/boot-result.json').read_text())['sha256']
rec['bootloader_max_download_bytes']=limit

rec.update(state='ONE_TEMPORARY_BOOT_STARTED',attempts=1,boot_commands=1);state.write_text(json.dumps(rec,indent=2)+'\n')
try:d.run('fastboot',['boot',str(p)],timeout=180);rec['state']='TRANSPORT_ACCEPTED_SCREEN_PENDING'
except Exception as e:rec.update(state='FAILED_STOP_NO_RETRY',error=str(e));raise
finally:state.write_text(json.dumps(rec,indent=2)+'\n')
print(json.dumps(rec,indent=2))
