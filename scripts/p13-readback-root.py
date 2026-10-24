#!/usr/bin/env python3
"""Verify the deployed P12 bytes by on-device hashing, without backing up userdata."""
from pathlib import Path
import hashlib,json,subprocess,sys,time
r=Path(__file__).resolve().parents[1];sys.path.insert(0,str(r/'tools/p13'));from device import Device
d=Device('root-readback');d.require_root_recovery();assert d.shell('getprop ro.product.device')=='cheeseburger';assert d.shell('readlink -f /dev/block/bootdevice/by-name/userdata')=='/dev/block/sda14'
i=json.loads((r/'out/p13/inputs.json').read_text());assert i['image_bytes']%1048576==0
assert not any(x.split()[0]=='/dev/block/sda14' for x in d.shell('cat /proc/mounts').splitlines())
cmd=[str(r/'.work/p3-tools-35/platform-tools/adb'),'-s',d.serial,'exec-out','dd if=/dev/block/sda14 bs=4096 count=1 2>/dev/null'];v=subprocess.run(cmd,env=d.env,capture_output=True,timeout=30);assert v.returncode==0 and len(v.stdout)==4096
with (r/i['image']).open('rb') as f:assert v.stdout==f.read(4096)
(d.out/'shngroot-header.bin').write_bytes(v.stdout)
result={'state':'HEADER_MATCHED_HASHING_DEPLOYED_IMAGE','run_dir':str(d.out.relative_to(r)),'device_path':'/dev/block/sda14','shngroot_header_matches_p12':True,'device_image_bytes':i['image_bytes'],'expected_sha256':i['image_sha256'],'userdata_backup':False,'partition_writes':False};p=r/'out/p13/readback.json';p.write_text(json.dumps(result,indent=2)+'\n')
# No USB transfer of private old userdata. The phone hashes only the deployed root window.
count=i['image_bytes']//1048576;command='dd if=/dev/block/sda14 bs=1048576 count='+str(count)+' 2>/dev/null | sha256sum';t=time.time();text=d.run('adb',['shell',command],timeout=1200);actual=text.split()[0];assert actual==i['image_sha256'];assert not any(x.split()[0]=='/dev/block/sda14' for x in d.shell('cat /proc/mounts').splitlines())
result.update(state='DEPLOYED_P12_ROOT_SHA256_PASS',sha256=actual,elapsed_seconds=time.time()-t,read_command=command,read_exit_code=0,sha256_match=True,header_read_exit_code=v.returncode,verification_scope='Exact first 9382658048 deployed bytes, not the remaining userdata or a mounted filesystem');p.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
