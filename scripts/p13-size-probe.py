#!/usr/bin/env python3
"""One controlled RAM-size isolation image: identical FD/header/DTB, config-only initrd."""
from pathlib import Path
import hashlib,json,struct,subprocess,sys,zlib
r=Path(__file__).resolve().parents[1];o=r/'out/p13/size-probe';o.mkdir(exist_ok=True);sys.path.insert(0,str(r/'tools/ram_payload'));import bundle;bundle.MAX_BYTES=256*1024*1024
config=b'version = 1\n[boot]\ntimeout = 10\ndefault = "exit"\n[ui]\ntitle = "P13 SIZE PROBE - UEFI ONLY"\n[[entry]]\nid = "exit"\ntitle = "Return to firmware - no XNU in this diagnostic"\ntype = "action"\naction = "exit"\n'
payload=bundle.pack({'\\EFI\\UniXNU\\unixnu.toml':config});p=o/'config-only.bundle';p.write_bytes(payload)
source=json.loads((r/'out/p12/uefi/packaging.json').read_text());cmd=list(source['command']);cmd[0]=sys.executable;cmd[cmd.index('--ramdisk')+1]=str(p);image=o/'boot-p13-size-probe.img';cmd[cmd.index('-o')+1]=str(image);subprocess.run(cmd,check=True)
b=image.read_bytes();ks,ka,rs,ra,ss,sa,ta,page,ver,_=struct.unpack_from('<10I',b,8);assert (ka,ra,ta,page,ver,ss)==(0x8000,0x1000000,0x100,4096,0,0)
d=zlib.decompressobj(31);fd=d.decompress(b[page:page+ks])+d.flush();assert hashlib.sha256(fd).hexdigest()==source['fd_sha256'];assert d.unused_data==(r/'out/p12/uefi/Android-cheeseburger.dtb').read_bytes();at=page+(ks+page-1)//page*page;assert bundle.verify(b[at:at+rs])=={'\\EFI\\UniXNU\\unixnu.toml':config}
meta={'purpose':'RAM-initrd-size isolation only; NO XNU or root mount expected','image':str(image.relative_to(r)),'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest(),'firmware_sha256':source['fd_sha256'],'payload_bytes':len(payload),'kernel_payload_present':False,'userdata_writes':False,'p12_original_files_changed':False,'command':cmd};(o/'manifest.json').write_text(json.dumps(meta,indent=2)+'\n');print(json.dumps(meta,indent=2))
