#!/usr/bin/env python3
from pathlib import Path
import sys,json,zipfile,hashlib,subprocess,struct,re
r=Path(__file__).resolve().parents[1];sys.path.insert(0,str(r.parent/'upstream/UniXNU/scripts'));from unixnu.ipsw import RangeFile,im4p_payload
p=r/'out/p8/ios';m=json.loads((p/'apple-manifest.json').read_text());identity=m['identities'][0];z=zipfile.ZipFile(RangeFile(m['source'],block=256*1024,retries=2));items={}
for component in ['KernelCache','DeviceTree','StaticTrustCache']:
 spec=identity['components'][component];name=spec['path'];dest=p/Path(name).name
 if dest.exists() and hashlib.sha384(dest.read_bytes()).hexdigest()==spec['digest']:b=dest.read_bytes()
 else:b=z.read(name);dest.write_bytes(b)
 assert len(b)==spec['zip_bytes'] and len(b)<32*1024*1024
 items[component]=dict(file=str(dest.relative_to(r)),bytes=len(b),sha256=hashlib.sha256(b).hexdigest(),sha384=hashlib.sha384(b).hexdigest(),manifest_digest=spec['digest'],digest_matches_im4p=hashlib.sha384(b).hexdigest()==spec['digest'])
packed=p/'iphone-kernelcache.lzfse';packed.write_bytes(im4p_payload((p/Path(identity['components']['KernelCache']['path']).name).read_bytes()));raw=p/'iphone-kernelcache.macho'
subprocess.run(['/usr/bin/compression_tool','-decode','-a','lzfse','-i',str(packed),'-o',str(raw)],check=True);b=raw.read_bytes()
if b[:4]==bytes.fromhex('cafebabe'):
 slices=[struct.unpack_from('>5I',b,8+20*i) for i in range(struct.unpack_from('>I',b,4)[0])];arm=[x for x in slices if x[0]==0x100000c];assert len(arm)==1;_,_,at,n,_=arm[0];b=b[at:at+n];raw.write_bytes(b)
assert b[:4]==bytes.fromhex('cffaedfe');cpu,subtype=struct.unpack_from('<II',b,4);versions=[x.decode(errors='replace') for x in re.findall(rb'Darwin Kernel Version[^\x00]{1,250}',b)];result=dict(result='BLOCKED',components=items,cpu=hex(cpu),subtype=hex(subtype),arm64e=(subtype&0xffffff)==2,versions=versions,kernel_sha256=hashlib.sha256(b).hexdigest(),current_xnu='10063.141.1 / Darwin 23.6.0 / ARM64 ordinary subtype',systemos_component='Cryptex1,SystemOS',reason='Do not prelink arm64e/PAC iPhone drivers into ordinary ARM64 MSM8998 kernel. Keep candidate separate from boot payload pending actual ABI validation.',mixed_with_p8=False,apple_signature_verified=False)
(p/'iphone-abi.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
