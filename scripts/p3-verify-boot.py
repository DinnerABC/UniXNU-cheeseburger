#!/usr/bin/env python3
"""Verify fixed P2 images and real-device matching evidence before temporary boot."""
import hashlib,json,struct,zlib
from pathlib import Path
r=Path(__file__).resolve().parents[1]
expected={'baseline':'c52e0ab0100a8bfb46c6d66c888455147d28634b6b88ab1d93599d7b87ca6433','integrated':'dc599569ca8a9edd5f1d6c3a6edf37bc61e8e5b1636220b4b3c00e73a87d12e8'}
comparison=json.loads((r/'out/p3/dtb-comparison.json').read_text())
assert comparison['runtime']['root']==comparison['uefi']['root'],'Observed platform IDs differ'
assert comparison['runtime']['root']['qcom,msm-id']==[292,131073]
valid={}
gpt=[]
for manifest in (r/'backups/p3').glob('*/manifest.json'):
 for rec in json.loads(manifest.read_text()):
  if rec.get('verified') and 'partition' in rec:valid[rec['partition']]=rec
  if rec.get('verified') and 'lun' in rec:gpt.append(rec)
for name in ['persist','modemst1','modemst2','fsg','boot','recovery','abl','xbl','tz','rpm','hyp','devcfg']:
 rec=valid[name];p=r/rec['file']
 assert p.stat().st_size==rec['expected_bytes']
 h=hashlib.sha256(p.read_bytes()).hexdigest()
 assert h==rec['sha256']==rec['device_sha256_before']==rec['device_sha256_after']
assert {rec['lun'] for rec in gpt}=={'sda','sdb','sdc','sdd','sde','sdf'}
for rec in gpt:
 for f in rec['files']:
  p=r/f['file'];assert len(p.read_bytes())==f['bytes'] and hashlib.sha256(p.read_bytes()).hexdigest()==f['sha256']
report={}
for name,expected_hash in expected.items():
 p=r/'out/p2'/name/'boot-cheeseburger.img';b=p.read_bytes()
 assert hashlib.sha256(b).hexdigest()==expected_hash and b[:8]==b'ANDROID!'
 ks,ka,rs,ra,ss,sa,ta,page,version,osv=struct.unpack_from('<10I',b,8)
 assert version==1 and page==2048 and ss==0 and rs==1
 d=zlib.decompressobj(31);fd=d.decompress(b[page:page+ks])+d.flush();dt=d.unused_data
 assert fd==(r/'out/p2'/name/'MSM8998PKG_UEFI.fd').read_bytes() and fd[56:60]==b'ARMd'
 assert dt==(r/'out/p2'/name/'Android-cheeseburger.dtb').read_bytes()
 ramoffset=page+((ks+page-1)//page)*page
 assert b[ramoffset:ramoffset+rs]==b'\n'
 report[name]=dict(file=str(p.relative_to(r)),sha256=expected_hash,bytes=len(b),header_version=version,page_size=page,kernel_bytes=ks,ramdisk_bytes=rs,arm64_header=True,fd_bytes=len(fd),platform_ids_match=True,dtb_byte_identical_to_runtime=False,temporary_boot_preflight='PASS',hardware_boot_proven=False)
(r/'out/p3/temporary-boot-preflight.json').write_text(json.dumps(report,indent=2)+'\n')
print('Both fixed P2 images: PASS; matched actual SoC/board IDs; key backups rehashed; all six LUN GPTs verified')
