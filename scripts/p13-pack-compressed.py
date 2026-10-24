from pathlib import Path
import hashlib,json,lzma,struct,subprocess,sys,zlib,gzip
r=Path(__file__).resolve().parents[1];o=r/'out/p13/compressed';u=o/'uefi'
assert json.loads((r/'out/p13/logs/build-compressed-uefi-01.json').read_text())['exit_code']==0
assert json.loads((o/'fixed-decoder-test.json').read_text())['result']=='FIXED_EDK2_SDK_HOST_PASS'
v=json.loads((u/'image-verification.json').read_text());assert v['firmware']['result']==v['bootimg']['result']=='PASS'
payload=(o/'payload.container').read_bytes();magic,cl,dl,sha=struct.unpack_from('<8sQQ32s',payload);assert magic==b'P13LZ001' and len(payload)==56+cl
original=lzma.decompress(payload[56:],format=lzma.FORMAT_ALONE);assert len(original)==dl==124993536;assert hashlib.sha256(original).digest()==sha;assert sha.hex()=='21af3f6eb418c3103ab4eed473cf43e148988568e5172fd69c64b3a3df049194'
fd=(u/'MSM8998PKG_UEFI.fd').read_bytes();dtb=(u/'Android-cheeseburger.dtb').read_bytes();assert dtb==(r/'out/p12/uefi/Android-cheeseburger.dtb').read_bytes()
k=u/'kernel.gz-dtb';k.write_bytes(gzip.compress(fd,mtime=0)+dtb)
source=json.loads((r/'out/p12/uefi/packaging.json').read_text());cmd=list(source['command']);cmd[0]=sys.executable;cmd[cmd.index('--kernel')+1]=str(k);cmd[cmd.index('--ramdisk')+1]=str(o/'payload.container');image=u/'boot-p13-compressed.img';cmd[cmd.index('-o')+1]=str(image);subprocess.run(cmd,check=True)
b=image.read_bytes();ks,ka,rs,ra,ss,sa,ta,page,ver,_=struct.unpack_from('<10I',b,8);assert (ka,ra,ta,page,ver,ss)==(0x8000,0x1000000,0x100,4096,0,0)
d=zlib.decompressobj(31);assert d.decompress(b[page:page+ks])+d.flush()==fd and d.unused_data==dtb;at=page+(ks+page-1)//page*page;assert b[at:at+rs]==payload
m=dict(result='OFFLINE_PACKAGING_PASS',image=str(image.relative_to(r)),bytes=len(b),sha256=hashlib.sha256(b).hexdigest(),firmware_sha256=hashlib.sha256(fd).hexdigest(),payload_sha256=hashlib.sha256(payload).hexdigest(),expanded_payload_sha256=sha.hex(),ramdisk_bytes=rs,original_kernel_adt_trustcache_config_unchanged=True,userdata_writes=False,hardware_validated=False,command=cmd)
(u/'packaging.json').write_text(json.dumps(m,indent=2)+'\n');print(json.dumps(m,indent=2))
