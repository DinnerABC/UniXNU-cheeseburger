#!/usr/bin/env python3
"""Local-only SHNGROOT builder using verified cheeseburger GPT geometry.
Reads attached DMGs read-only; never emits a phone write command. Does not
substitute images from a different BuildIdentity when components are absent.
"""
from pathlib import Path
import argparse,json,struct,zlib,hashlib,subprocess,plistlib,re,os
r=Path(__file__).resolve().parents[1];o=r/'out/p8/root';o.mkdir(parents=True,exist_ok=True)
def sha(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
 return h.hexdigest()
def geometry():
 records=json.loads((r/'backups/p3/gpt-20261022-023658/manifest.json').read_text());found=[]
 for record in records:
  assert record['verified'];entry=record['files'][0];p=r/entry['file'];assert sha(p)==entry['sha256'];b=p.read_bytes();sector=record['logical_sector_size'];h=b[sector:2*sector];assert h[:8]==b'EFI PART';hs,crc=struct.unpack_from('<II',h,12);header=bytearray(h[:hs]);header[16:20]=bytes(4);assert zlib.crc32(header)==crc
  at,count,esize,ecrc=struct.unpack_from('<QIII',h,72);entries=b[at*sector:at*sector+count*esize];assert len(entries)==count*esize and zlib.crc32(entries)==ecrc
  for i in range(count):
   e=entries[i*esize:(i+1)*esize]
   if e[:16]==bytes(16):continue
   name=e[56:128].decode('utf-16le').split('\0')[0]
   if name=='userdata':
    first,last=struct.unpack_from('<QQ',e,32);assert first<=last<=record['primary']['last_usable'];found.append(dict(name=name,lun=record['lun'],lba_bytes=sector,first_lba=first,last_lba=last,capacity=(last-first+1)*sector,gpt_sha256=entry['sha256']))
 assert len(found)==1;return found[0]
a=argparse.ArgumentParser();a.add_argument('--inputs',type=Path,help='JSON generated from actual extracted Apple members, with raw SHA256 pins');a.add_argument('--geometry-only',action='store_true');args=a.parse_args();g=geometry();(o/'geometry.json').write_text(json.dumps(g,indent=2)+'\n')
if args.geometry_only:print(json.dumps(g,indent=2));raise SystemExit(0)
if not args.inputs:
 (o/'result.json').write_text(json.dumps(dict(result='BLOCKED',geometry=g,reason='No authenticated and ABI-compatible OS/SystemOS/APFS/TrustCache inputs. No userdata-root.img created.',hardware_tested=False,safe_to_flash=False),indent=2)+'\n');raise SystemExit('Missing --inputs; no root image fabricated')
spec=json.loads(args.inputs.read_text());apple=json.loads((r/'out/p8/ios/apple-manifest.json').read_text());assert spec['manifest_sha256']==apple['manifest_sha256'];identity=next(x for x in apple['identities'] if x['index']==spec['identity_index'])
# Binary driver ABI must have a real validation record; metadata alone is not enough.
assert spec['kernel_abi_validation']['result']=='PASS' and spec['kernel_abi_validation']['kernel_sha256']==sha(r/'out/p8/kernel/kernel.macho')
layout=[];cursor=1024*1024
for key in ['OS','SystemOS']:
 component=identity['components'].get('Cryptex1,SystemOS' if key=='SystemOS' else key);assert component,'Selected BuildIdentity lacks '+key
 item=spec['components'][key];assert item['member']==component['path'];p=(r/item['file']).resolve();assert p.is_relative_to(r) and p.is_file() and sha(p)==item['sha256']
 subprocess.run(['hdiutil','verify',str(p)],check=True)
 info=plistlib.loads(subprocess.check_output(['hdiutil','imageinfo','-plist',str(p)]));size=info['Size Information']['Total Bytes'];assert size>0 and size%512==0
 layout.append((key,p,cursor,size));cursor+=(size+1048575)//1048576*1048576
assert cursor<=g['capacity'],'Root windows exceed actual userdata capacity'
image=o/'userdata-root.img';tmp=o/'userdata-root.img.partial';entries=[]
try:
 with tmp.open('xb') as target:
  target.truncate(cursor)
  for key,p,offset,size in layout:
   attach=plistlib.loads(subprocess.check_output(['hdiutil','attach','-plist','-nomount','-readonly','-noautofsck',str(p)]));dev=next(x['dev-entry'] for x in attach['system-entities'] if re.fullmatch(r'/dev/disk\d+',x.get('dev-entry','')));digest=hashlib.sha256()
   try:
    target.seek(offset);remaining=size
    with open(dev.replace('/dev/disk','/dev/rdisk'),'rb',buffering=0) as source:
     while remaining:
      b=source.read(min(8<<20,remaining));assert b,'Short read';target.write(b);digest.update(b);remaining-=len(b)
   finally:subprocess.run(['hdiutil','detach',dev],check=True)
   entries.append((key,offset,size,digest.digest()))
  h=bytearray(4096);h[:8]=b'SHNGROOT';struct.pack_into('<IIQ',h,8,1,len(entries),cursor)
  for i,(name,offset,size,digest) in enumerate(entries):struct.pack_into('<32sQQII32s40s',h,24+128*i,name.encode(),offset,size,512,1,digest,bytes(40))
  h[4064:]=hashlib.sha256(h[:4064]).digest();target.seek(0);target.write(h);target.flush();os.fsync(target.fileno())
 # Full readback of the written windows before promotion.
 with tmp.open('rb') as f:
  for name,offset,size,digest in entries:
   f.seek(offset);check=hashlib.sha256();remaining=size
   while remaining:
    b=f.read(min(8<<20,remaining));assert b;check.update(b);remaining-=len(b)
   assert check.digest()==digest
 assert not image.exists();tmp.rename(image)
 (o/'result.json').write_text(json.dumps(dict(result='PASS',image=str(image.relative_to(r)),bytes=cursor,sha256=sha(image),geometry=g,windows=[dict(name=n,offset=at,bytes=size,sha256=d.hex()) for n,at,size,d in entries],hardware_tested=False,safe_to_flash=False),indent=2)+'\n')
except Exception:
 # Keep partial evidence; never silently reuse it as a finished image.
 raise
