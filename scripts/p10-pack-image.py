#!/usr/bin/env python3
from pathlib import Path
import sys,json,subprocess,hashlib,struct,zlib,gzip,uuid
r=Path(__file__).resolve().parents[1];sys.path.insert(0,str(r/'tools/ram_payload'));import bundle;bundle.MAX_BYTES=64*1024*1024
from bundle import verify,manifest
p=r/'out/p10/payload/payload.bundle';payload=p.read_bytes();files=verify(payload);s=r.parent/'upstream/edk2-msm8998';o=r/'out/p10/uefi'
f=o/'MSM8998PKG_UEFI.fd';fd=f.read_bytes();assert len(fd)==0x600000 and fd[56:60]==b'ARMd'
m=json.loads((o/'compiled-efi-manifest.json').read_text());
# A matching FV/PE pair alone can still be stale after a failed rebuild.
# Bind the actual loader binary's compiled hash gate to this RAM payload.
loader=(o/'efi/DarwinBoot.efi').read_bytes()
for payload_name in ['\\kernel.macho','\\kernel.macho.adt']:
 assert hashlib.sha256(files[payload_name]).digest() in loader, 'Compiled DarwinBoot hash gate is stale: '+payload_name
for name,guid in [('P9Probe.efi','2E2DE31D-B845-4A39-91E3-1255D4212CDD'),('P4RamPayload.efi','970879CB-72F0-4876-A365-4BA3E9BA99A0'),('DarwinBoot.efi','1B64AF87-8415-4B57-9B16-680D70913B9F')]:
 assert m['built_pe_images'][name]['sha256']==m['fv_pe_sections'][guid]['sha256']
dtb=(s/'Platforms/Msm8998Pkg/Device/oneplus/cheeseburger/DeviceTreeBlob/Android/Android-cheeseburger.dtb').read_bytes();k=o/'kernel.gz-dtb';k.write_bytes(gzip.compress(fd,mtime=0)+dtb);image=o/'boot-p10-xnu.img'
cmd=[sys.executable,str(s/'mkbootimg.py'),'--kernel',str(k),'--ramdisk',str(p),'--base','0','--kernel_offset','0x8000','--ramdisk_offset','0x1000000','--tags_offset','0x100','--pagesize','4096','--header_version','0','-o',str(image)]
subprocess.run(cmd,check=True);b=image.read_bytes();ks,ka,rs,ra,ss,sa,ta,page,ver,_=struct.unpack_from('<10I',b,8);assert (ka,ra,ta,page,ver,ss)==(0x8000,0x1000000,0x100,4096,0,0)
d=zlib.decompressobj(31);assert d.decompress(b[page:page+ks])+d.flush()==fd and d.unused_data==dtb
at=page+((ks+page-1)//page)*page;assert b[at:at+rs]==payload;sha=hashlib.sha256(b).hexdigest()
(o/'packaging.json').write_text(json.dumps(dict(result='PASS',command=cmd,exit_code=0,image=str(image.relative_to(r)),bytes=len(b),sha256=sha,fd_sha256=hashlib.sha256(fd).hexdigest(),payload_sha256=hashlib.sha256(payload).hexdigest(),compiled_darwinboot_hash_gates_match=True,files=manifest(payload),hardware_validated=False,safe_to_flash=False,auto_xnu_entry=True),indent=2)+'\n')
(r/'config/p10/boot-profile.json').write_text(json.dumps(dict(image=str(image.relative_to(r)),sha256=sha,observe_seconds=240,max_attempts=1),indent=2)+'\n');print('P9 firmware, embedded PE, DTB, kernel and ADT roundtrip verified',sha)
