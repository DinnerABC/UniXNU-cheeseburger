#!/usr/bin/env python3
"""Repeat the pinned upstream's native UEFI Android-v1 packaging recipe only."""
import argparse,json,re,subprocess,hashlib,struct,zlib
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--fd',type=Path,required=True);p.add_argument('--source',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
a.output.mkdir(parents=True,exist_ok=True);cfg=(a.source/'Config/cheeseburger.conf').read_text()
def config(key):
 m=re.search(r'^'+key+r'=(.+)$',cfg,re.M);assert m,key
 return m[1].strip().strip('"')
data=a.fd.read_bytes();assert len(data)==0x600000 and data[56:60]==b'ARMd'
kernel=a.output/'uefi-cheeseburger.img.gz-dtb';ramdisk=a.output/'ramdisk';ramdisk.write_bytes(b'\n')
with kernel.open('wb') as f:subprocess.run(['gzip','-c'],input=data,stdout=f,check=True)
with kernel.open('ab') as f:f.write((a.source/'Platforms/Msm8998Pkg/Device/oneplus/cheeseburger/DeviceTreeBlob/Android/Android-cheeseburger.dtb').read_bytes())
cmd=['python3',str(a.source/'mkbootimg.py'),'--kernel',str(kernel),'--ramdisk',str(ramdisk),'--kernel_offset','0x00000000','--ramdisk_offset','0x00000000','--tags_offset','0x00000000','--os_version',config('BOOTIMG_OS_VERSION'),'--os_patch_level',config('BOOTIMG_OS_PATCH_LEVEL'),'--header_version','1','-o',str(a.output/'boot-cheeseburger.img')]
subprocess.run(cmd,check=True)
f=a.output/'boot-cheeseburger.img'
b=f.read_bytes();assert b[:8]==b'ANDROID!'
ks=struct.unpack_from('<I',b,8)[0];page=struct.unpack_from('<I',b,36)[0]
assert struct.unpack_from('<I',b,40)[0]==1
stream=zlib.decompressobj(16+zlib.MAX_WBITS)
assert stream.decompress(b[page:page+ks])+stream.flush()==data
assert stream.unused_data==(a.source/'Platforms/Msm8998Pkg/Device/oneplus/cheeseburger/DeviceTreeBlob/Android/Android-cheeseburger.dtb').read_bytes()

(a.output/'packaging.json').write_text(json.dumps({'command':cmd,'exit_code':0,'bytes':f.stat().st_size,'sha256':hashlib.sha256(f.read_bytes()).hexdigest(),'roundtrip_fd_and_dtb':True,'native_uefi_only':True,'hardware_validated':False,'safe_to_flash':False},indent=2)+'\n')
