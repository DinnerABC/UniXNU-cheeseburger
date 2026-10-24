#!/usr/bin/env python3
"""Transition to bootloader once, then cross-check physical target and download limits."""
from pathlib import Path
import json,re,sys,time
r=Path(__file__).resolve().parents[1];sys.path.insert(0,str(r/'tools/p13'));from device import Device
inputs=json.loads((r/'out/p13/inputs.json').read_text());geometry=json.loads((r/'out/p13/device-geometry.json').read_text())
recovery=json.loads((r/'out/p13/recovery-check.json').read_text());assert recovery['result']=='ANDROID_REINITIALIZATION_CAPABILITY_PASS'
cancelled=json.loads((r/'out/p13/private/userdata-rollback/manifest.json').read_text());assert cancelled['state']=='USER_CANCELLED_NO_USERDATA_BACKUP' and not cancelled['verified']
assert json.loads((r/'out/p13/backup-check.json').read_text())['result']=='P3_LOCAL_BACKUP_SHA256_PASS'
d=Device('fastboot-gate')
if '--resume-bootloader' in sys.argv:
 prior=r/'out/p13/private/20261023T041343Z-fastboot-gate-0da2a5'
 step=json.loads((prior/'005.json').read_text());assert step['exit_code']==0 and step['command'][-2:]==['reboot','bootloader'] and step['command'][2]==d.serial
 assert (prior/'003.log').read_text().strip()=='cheeseburger'
else:
 d.require_root_recovery();assert d.shell('getprop ro.product.device')=='cheeseburger';assert not any(x.split()[0]=='/dev/block/sda14' for x in d.shell('cat /proc/mounts').splitlines());d.run('adb',['reboot','bootloader'])
# No automatic reboot/recovery attempts. Only bounded observation after this transition.
for _ in range(30):
 try:
  product=d.run('fastboot',['getvar','product'],timeout=2);break
 except RuntimeError:time.sleep(2)
else:raise SystemExit('Bootloader not observed; no writes')
def value(text,name):
 match=re.search(r'(?:\(bootloader\)\s*)?'+re.escape(name)+r':[ \t]*([^\r\n]*)',text);assert match,(name,text);assert match[1].strip();return match[1].strip()
assert value(product,'product')=='msm8998'
var={}
for name in ['unlocked','partition-size:userdata','max-download-size']:
 var[name]=value(d.run('fastboot',['getvar',name]),name)
assert var['unlocked']=='yes'
info=d.run('fastboot',['oem','device-info']);assert re.search(r'Device unlocked:\s*true',info) and re.search(r'Device critical unlocked:\s*false',info)
assert int(var['partition-size:userdata'],0)==geometry['partition_bytes'];cap=int(var['max-download-size'],0);assert cap>=inputs['boot_bytes'] and cap>=128*1024*1024
optional={}
for name in ['is-userspace','is-logical:userdata','has-slot:userdata','version','version-bootloader','partition-type:userdata']:
 try:optional[name]=value(d.run('fastboot',['getvar',name]),name)
 except (RuntimeError,AssertionError):optional[name]=None
assert optional['is-userspace'] in [None,'no'] and optional['is-logical:userdata'] in [None,'no']
result={'result':'P13_WRITE_PREFLIGHT_PASS','run_dir':str(d.out.relative_to(r)),'vars':var,'optional_vars':optional,'unsupported_not_inferred_success':True,'mode_evidence':'root Recovery adb reboot bootloader + pinned serial/product + OEM device-info; not fastbootd','product_note':'msm8998 is SoC-only; exact cheeseburger and serial proven in preceding Recovery transition','userdata_physical_geometry_verified':True,'userdata_capacity':geometry['partition_bytes'],'max_download_bytes':cap,'sparse_chunk_bytes':128*1024*1024,'image':inputs['image'],'image_sha256':inputs['image_sha256'],'boot_image':inputs['boot_image'],'boot_sha256':inputs['boot_sha256'],'userdata_backup_present':False,'userdata_backup_user_declined':True,'android_empty_data_recovery_capability_verified':True,'restore_rehearsed':False,'authorized_partition':'userdata','write_attempts':0,'one_time_reference_experiment_authorized':True,'formal_iphone_installation_qualified':False,'apple_signature_bypass_enabled':False,'target_iomedia_proven':False,'data_seed_present':False,'root_mount_experiment_only':True,'created_epoch':time.time()}
(r/'out/p13/preflight.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
