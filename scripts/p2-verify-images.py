#!/usr/bin/env python3
"""Validate the real upstream FD and Android v1 boot image without touching hardware."""
import argparse,gzip,hashlib,json,struct,zlib
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
s=a.source.resolve();o=a.output.resolve();o.mkdir(parents=True,exist_ok=True)
fd=s/'workspace/Build/cheeseburger/RELEASE_GCC5/FV/MSM8998PKG_UEFI.fd'
boot=o/'boot-cheeseburger.img'
report={'hardware_validated':False,'safe_to_flash':False,'firmware':{'result':'FAIL'},'bootimg':{'result':'FAIL'}}
def meta(f):return {'path':str(f),'bytes':f.stat().st_size,'sha256':hashlib.sha256(f.read_bytes()).hexdigest()}
try:
 data=fd.read_bytes()
 # These are observed from the pinned cheeseburger FDF, not guessed phone parameters.
 assert len(data)==0x600000,'FD size differs from original FDF'
 assert data[56:60]==b'ARMd','FD missing original ARM64 image header'
 assert data[0x8000+40:0x8000+44]==b'_FVH','Missing compact FV signature'
 report['firmware']={'result':'PASS',**meta(fd),'arm64_header':True,'fv_offset':0x8000}
 dest=o/'MSM8998PKG_UEFI.fd';dest.write_bytes(data)
 b=boot.read_bytes();assert b[:8]==b'ANDROID!'
 fields=struct.unpack_from('<10I',b,8)
 kernel_size,kernel_addr,ramdisk_size,ramdisk_addr,second_size,second_addr,tags_addr,pagesize,version,osversion=fields
 assert version==1 and pagesize>=2048 and pagesize&(pagesize-1)==0
 kernel=b[pagesize:pagesize+kernel_size]
 d=zlib.decompressobj(16+zlib.MAX_WBITS);unpacked=d.decompress(kernel)+d.flush()
 assert unpacked==data,'Boot kernel gzip differs from real FD'
 dtb=s/'Platforms/Msm8998Pkg/Device/oneplus/cheeseburger/DeviceTreeBlob/Android/Android-cheeseburger.dtb'
 assert d.unused_data==dtb.read_bytes(),'Appended DTB differs from original device DTB'
 assert d.unused_data[:4]==bytes.fromhex('d00dfeed')
 ramstart=pagesize+((kernel_size+pagesize-1)//pagesize)*pagesize
 assert b[ramstart:ramstart+ramdisk_size]==(s/'ramdisk').read_bytes()
 report['bootimg']={'result':'PASS',**meta(boot),'header_version':version,'page_size':pagesize,'kernel_size':kernel_size,'ramdisk_size':ramdisk_size,'roundtrip_fd_and_dtb':True,'dtb':meta(dtb)}
except (OSError,AssertionError,ValueError,zlib.error,struct.error) as e:report['error']=str(e)
(o/'image-verification.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
raise SystemExit(0 if report['firmware']['result']==report['bootimg']['result']=='PASS' else 1)
