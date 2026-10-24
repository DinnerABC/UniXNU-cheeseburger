#!/usr/bin/env python3
from pathlib import Path
import hashlib,json,struct,sys,zlib
r=Path(__file__).resolve().parents[1];sys.path.insert(0,str(r/'tools/p13'));from device import Device
d=Device('geometry');d.require_root_recovery();assert d.shell('getprop ro.product.device')=='cheeseburger'
mapping=d.shell('readlink -f /dev/block/bootdevice/by-name/userdata');assert mapping=='/dev/block/sda14'
ss=int(d.shell('blockdev --getss /dev/block/sda'));size=int(d.shell('blockdev --getsize64 /dev/block/sda'));part=int(d.shell('blockdev --getsize64 '+mapping));assert ss==4096
result={'run_dir':str(d.out.relative_to(r)),'physical_partition':mapping,'block_bytes':ss,'lun_bytes':size,'partition_bytes':part,'hardware_identity':'cheeseburger','partition_is_physical':True}
def raw(command):
 d.n+=1;cmd=[str(r/'.work/p3-tools-35/platform-tools/adb'),'-s',d.serial,'exec-out',command]
 import subprocess
 q=subprocess.run(cmd,env=d.env,capture_output=True,timeout=60);(d.out/(str(d.n).zfill(3)+'.json')).write_text(json.dumps({'command':cmd,'exit_code':q.returncode}));assert q.returncode==0;return q.stdout
for label,lba in [('primary',1),('backup',size//ss-1)]:
 h=raw('dd if=/dev/block/sda bs=4096 skip='+str(lba)+' count=1 2>/dev/null');assert len(h)==ss and h[:8]==b'EFI PART'
 n,crc=struct.unpack_from('<II',h,12);head=bytearray(h[:n]);head[16:20]=bytes(4);assert zlib.crc32(head)==crc
 cur,other=struct.unpack_from('<QQ',h,24);assert cur==lba
 at,count,es,ec=struct.unpack_from('<QIII',h,72);assert 128<=es<=4096 and count*es<=16*1024*1024
 data=raw('dd if=/dev/block/sda bs=4096 skip='+str(at)+' count='+str((count*es+ss-1)//ss)+' 2>/dev/null')[:count*es];assert zlib.crc32(data)==ec
 if label=='primary':table=data
 else:assert table==data
 (d.out/(label+'-header.bin')).write_bytes(h);(d.out/(label+'-entries.bin')).write_bytes(data)
 entries=[]
 for i in range(count):
  e=data[i*es:(i+1)*es]
  if e[:16]==bytes(16):continue
  name=e[56:128].decode('utf-16-le').rstrip('\0');first,last=struct.unpack_from('<QQ',e,32)
  if name=='userdata':entries.append({'gpt_index':i+1,'first_lba':first,'last_lba':last,'bytes':(last-first+1)*ss})
 assert len(entries)==1 and entries[0]['gpt_index']==14
 expected=json.loads((r/'out/p12/geometry.json').read_text())['userdata'];entry=entries[0]
 assert entry['first_lba']==expected['first_lba'] and entry['last_lba']==expected['last_lba'] and entry['bytes']==part==expected['capacity_bytes']
 result[label]={'header_crc_verified':True,'entries_crc_verified':True,'entries_sha256':hashlib.sha256(data).hexdigest(),'userdata':entry,'alternate_lba':other}
result['sysfs_partition_start_512_sectors']=int(d.shell('cat /sys/class/block/sda14/start'));assert result['sysfs_partition_start_512_sectors']*512==entry['first_lba']*ss
result['mounts']=d.shell('cat /proc/mounts');result['filesystem_tools']=d.shell('ls -l /sbin/twrp /sbin/mke2fs /sbin/mkfs.ext4 /sbin/resize2fs 2>/dev/null || true')
header=raw('dd if='+mapping+' bs=4096 count=1 2>/dev/null');assert len(header)==4096;(d.out/'current-userdata-header.bin').write_bytes(header);result['current_shngroot_magic']=header[:8]==b'SHNGROOT';result['current_header_sha256']=hashlib.sha256(header).hexdigest()
(r/'out/p13/device-geometry.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k not in ['mounts']},indent=2))
