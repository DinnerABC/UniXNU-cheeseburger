from pathlib import Path
import hashlib,json,lzma,struct,subprocess,sys,zlib,gzip
r=Path(__file__).resolve().parents[4];o=r/'out/p23/ios-16.7.16-20H392/payload';u=r/'out/p23/ios-16.7.16-20H392/uefi'
assert json.loads((r/'out/p23/ios-16.7.16-20H392/logs/build-uefi-01.json').read_text())['exit_code']==0
# Full decoder regression skipped at user request; roundtrip remains below.
# Firmware build success is required above; no full export audit.
payload=(o/'payload.container').read_bytes();magic,cl,dl,sha=struct.unpack_from('<8sQQ32s',payload);assert magic==b'P13LZ001' and len(payload)==56+cl
original=lzma.decompress(payload[56:],format=lzma.FORMAT_ALONE);pm=json.loads((o/'manifest.json').read_text());assert len(original)==dl==pm['bytes'];assert hashlib.sha256(original).digest()==sha;assert sha.hex()==pm['sha256']
fd=(u/'MSM8998PKG_UEFI.fd').read_bytes();assert len(fd)==6291456 and fd[0x38:0x3c]==b'ARM\x64';dtb=(u/'Android-cheeseburger.dtb').read_bytes();assert dtb==(r/'out/p12/uefi/Android-cheeseburger.dtb').read_bytes()
k=u/'kernel.gz-dtb';k.write_bytes(gzip.compress(fd,mtime=0)+dtb)
source=json.loads((r/'out/p12/uefi/packaging.json').read_text());cmd=list(source['command']);cmd[0]=sys.executable;cmd[cmd.index('--kernel')+1]=str(k);cmd[cmd.index('--ramdisk')+1]=str(o/'payload.container');image=u/'boot-p23-stagec-20h392-8cpu-1g.img';cmd[cmd.index('-o')+1]=str(image);subprocess.run(cmd,check=True)
b=image.read_bytes();ks,ka,rs,ra,ss,sa,ta,page,ver,_=struct.unpack_from('<10I',b,8);assert (ka,ra,ta,page,ver,ss)==(0x8000,0x1000000,0x100,4096,0,0)
d=zlib.decompressobj(31);assert d.decompress(b[page:page+ks])+d.flush()==fd and d.unused_data==dtb;at=page+(ks+page-1)//page*page;assert b[at:at+rs]==payload
m=dict(result='OFFLINE_PACKAGING_PASS',image=str(image.relative_to(r)),bytes=len(b),sha256=hashlib.sha256(b).hexdigest(),firmware_sha256=hashlib.sha256(fd).hexdigest(),payload_sha256=hashlib.sha256(payload).hexdigest(),expanded_payload_sha256=sha.hex(),ramdisk_bytes=rs,hardware_adt_preserved_with_packed_slot=False,hardware_adt_change="add captured /psci method=smc; all existing nodes unchanged",trustcache_unchanged=True,root_auth_kernel_updated=True,kernel_unchanged_from_r5=False,userdata_writes=False,hardware_validated=False,command=cmd)
(u/'packaging.json').write_text(json.dumps(m,indent=2)+'\n');print(json.dumps(m,indent=2))
