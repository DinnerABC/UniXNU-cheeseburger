#!/usr/bin/env python3
from pathlib import Path
import json,sys,re
r=Path(__file__).resolve().parents[1];sys.path.insert(0,str(r/'tools/p13'));from device import Device
d=Device('recovery-capability');d.require_root_recovery();assert d.shell('getprop ro.product.device')=='cheeseburger'
commands={
 'tools':'command -v mke2fs; command -v make_ext4fs; command -v twrp; command -v recovery; true',
 'mke2fs_help':'mke2fs -h 2>&1 || true',
 'recovery_cli':'twrp 2>&1 || true',
 'recovery_fstab':'cat /etc/recovery.fstab; cat /etc/twrp.fstab 2>/dev/null || true',
 'android_partitions':'for p in boot recovery system vendor; do echo PARTITION=$p; readlink -f /dev/block/bootdevice/by-name/$p; blockdev --getsize64 /dev/block/bootdevice/by-name/$p 2>/dev/null; done; true',
 'boot_sha':'sha256sum /dev/block/bootdevice/by-name/boot',
 'recovery_sha':'sha256sum /dev/block/bootdevice/by-name/recovery',
 'mounts':'cat /proc/mounts'}
results={k:d.shell(v) for k,v in commands.items()}
assert 'mke2fs' in results['tools'] and 'Usage' in results['mke2fs_help']
assert 'userdata' in results['recovery_fstab'] and '/data' in results['recovery_fstab'] and 'ext4' in results['recovery_fstab']
assert 'PARTITION=system' in results['android_partitions'] and re.search(r'PARTITION=system\s+/dev/block/sd[a-z][0-9]+\s+[1-9][0-9]+',results['android_partitions'])
p3=json.loads((r/'backups/p3/20261022-023530/manifest.json').read_text());hashes={x['partition']:x['sha256'] for x in p3};assert results['boot_sha'].split()[0]==hashes['boot'] and results['recovery_sha'].split()[0]==hashes['recovery']
assert not any(x.split()[0]=='/dev/block/sda14' for x in results['mounts'].splitlines())
result={'result':'ANDROID_REINITIALIZATION_CAPABILITY_PASS','run_dir':str(d.out.relative_to(r)),'evidence':results,'android_boot_observed_before_experiment':True,'recovery_root_observed':True,'current_boot_and_recovery_match_verified_p3_backups':True,'system_partition_preserved':True,'data_backup_user_cancelled':True,'data_recovery_promised':False,'restore_strategy':'Existing Android boot/system retained; original Recovery can recreate ext4 userdata using its present formatter. This restores Android with empty user data, not prior apps/files. Only userdata may be reformatted in a separately authorized recovery action; never wipe/format other partitions.','restore_rehearsed':False,'device_partition_written':False}
(r/'out/p13/recovery-check.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k!='evidence'},indent=2))
