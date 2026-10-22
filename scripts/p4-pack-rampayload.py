#!/usr/bin/env python3
"""RAM probe image: actual OnePlus 5 stock/recovery header layout, no flash commands."""
from pathlib import Path
import sys,json,subprocess,hashlib,struct,zlib,gzip
r=Path(__file__).resolve().parents[1];sys.path.insert(0,str(r/'tools/ram_payload'));from bundle import verify,manifest
p=r/'out/cheeseburger/payload/ram-probe.bundle';payload=p.read_bytes();verify(payload)
s=r.parent/'upstream/edk2-msm8998';o=r/'out/p4/ram-payload-uefi';o.mkdir(parents=True,exist_ok=True)
f=o/'MSM8998PKG_UEFI.fd'
if not f.exists():f=o/'uefi-cheeseburger.fd'
if not f.exists():
 b=(o/'boot-cheeseburger.img').read_bytes();ks=struct.unpack_from('<I',b,8)[0];page=struct.unpack_from('<I',b,36)[0]
 d=zlib.decompressobj(31);raw=d.decompress(b[page:page+ks])+d.flush();assert len(raw)==0x600000 and raw[56:60]==b'ARMd';f.write_bytes(raw)
fd=f.read_bytes();assert len(fd)==0x600000 and fd[56:60]==b'ARMd'
dtb=(s/'Platforms/Msm8998Pkg/Device/oneplus/cheeseburger/DeviceTreeBlob/Android/Android-cheeseburger.dtb').read_bytes()
k=o/'ram-payload.kernel.gz-dtb';k.write_bytes(gzip.compress(fd,mtime=0)+dtb)
image=o/'boot-ram-probe.img'
cmd=[sys.executable,str(s/'mkbootimg.py'),'--kernel',str(k),'--ramdisk',str(p),'--base','0','--kernel_offset','0x8000','--ramdisk_offset','0x1000000','--tags_offset','0x100','--pagesize','4096','--header_version','0','-o',str(image)]
subprocess.run(cmd,check=True)
b=image.read_bytes();ks,ka,rs,ra,ss,sa,ta,page,version,_=struct.unpack_from('<10I',b,8)
assert (ka,ra,ta,page,version,ss)==(0x8000,0x1000000,0x100,4096,0,0)
d=zlib.decompressobj(31);assert d.decompress(b[page:page+ks])+d.flush()==fd and d.unused_data==dtb
at=page+((ks+page-1)//page)*page;assert b[at:at+rs]==payload
(o/'ram-probe-packaging.json').write_text(json.dumps(dict(result='PASS',command=cmd,exit_code=0,image=str(image.relative_to(r)),bytes=len(b),sha256=hashlib.sha256(b).hexdigest(),fd_sha256=hashlib.sha256(fd).hexdigest(),payload_sha256=hashlib.sha256(payload).hexdigest(),files=manifest(payload),header_layout_source='Actual P3 boot/recovery headers: page4096 v0 base0 k=0x8000 r=0x1000000 tags=0x100; no fixed runtime address assumed',hardware_validated=False,safe_to_flash=False),indent=2)+'\n');print('RAM probe boot image roundtrip verified:',len(b),'bytes')
