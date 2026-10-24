#!/usr/bin/env python3
"""Fetch only bounded Apple IPSW members via the fixed official range reader."""
from pathlib import Path
import argparse,hashlib,json,plistlib,re,struct,subprocess,sys,urllib.request,zipfile
r=Path(__file__).resolve().parents[1];sys.path.insert(0,str(r/'.work/p4-unixnu/scripts'))
from unixnu.ipsw import RangeFile,im4p_payload
urllib.request.install_opener(urllib.request.build_opener(urllib.request.ProxyHandler({})))
p=argparse.ArgumentParser();p.add_argument('candidate',choices=['A','B']);a=p.parse_args()
if a.candidate=='A':
 product,version,build,device='iPad7,1','17.7.11','21H461','j120ap'
 url='https://updates.cdn-apple.com/2025SpringFCS/fullrestores/122-77166/DCA6CFE8-90E9-4B92-880D-3DE171798E40/iPad_Pro_HFR_17.7.11_21H461_Restore.ipsw'
 discovery='Fixed UniXNU-sheng donor URL; identity is donor only, never cheeseburger board identity'
else:
 product,version,build,device='iPhone10,6','16.7.8','20H343','d221ap'
 listing=json.load(urllib.request.urlopen('https://api.ipsw.me/v4/device/iPhone10,6?type=ipsw',timeout=30))
 firmware=next(x for x in listing['firmwares'] if x['version']==version and x['buildid']==build)
 url=firmware['url'];discovery='ipsw.me catalogue for discovery; actual payload fetched only from Apple HTTPS'
assert url.startswith(('https://updates.cdn-apple.com/','https://updates-http.cdn-apple.com/','https://secure-appldnld.apple.com/'))
o=r/'out/p10/inputs'/a.candidate;o.mkdir(parents=True,exist_ok=True)
stream=RangeFile(url,block=256*1024,retries=2);z=zipfile.ZipFile(stream)
b=z.read('BuildManifest.plist');v=plistlib.loads(b);assert v['ProductVersion']==version and v['ProductBuildVersion']==build and product in v['SupportedProductTypes']
(o/'BuildManifest.plist').write_bytes(b)
identity=next(x for x in v['BuildIdentities'] if x['Info']['DeviceClass']==device and x['Info']['RestoreBehavior']=='Erase')
components={}
for component in ['KernelCache','StaticTrustCache','Cryptex1,SystemTrustCache']:
 spec=identity['Manifest'].get(component)
 if not spec:continue
 path=spec['Info']['Path'];info=z.getinfo(path);assert info.file_size<64*1024*1024
 dest=o/Path(path).name;digest=spec['Digest'];algo={20:'sha1',32:'sha256',48:'sha384'}[len(digest)]
 if dest.exists() and hashlib.new(algo,dest.read_bytes()).digest()==digest:raw=dest.read_bytes()
 else:
  raw=z.read(path);assert len(raw)==info.file_size and hashlib.new(algo,raw).digest()==digest
  dest.write_bytes(raw)
 components[component]=dict(file=str(dest.relative_to(r)),member=path,bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest(),manifest_digest_algorithm=algo,manifest_digest=digest.hex(),manifest_digest_match=True)
 packed=o/'kernelcache.lzfse';packed.write_bytes(im4p_payload((r/components['KernelCache']['file']).read_bytes()));raw=o/'kernelcache.macho'
subprocess.run(['/usr/bin/compression_tool','-decode','-a','lzfse','-i',str(packed),'-o',str(raw)],check=True)
b=raw.read_bytes()
if b[:4]==bytes.fromhex('cafebabe'):
 slices=[struct.unpack_from('>5I',b,8+20*i) for i in range(struct.unpack_from('>I',b,4)[0])];arm=[x for x in slices if x[0]==0x100000c];assert len(arm)==1
 _,_,at,n,_=arm[0];b=b[at:at+n];raw.write_bytes(b)
assert b[:4]==bytes.fromhex('cffaedfe')
cpu,subtype=struct.unpack_from('<II',b,4);versions=[x.decode(errors='replace') for x in re.findall(rb'Darwin Kernel Version[^\x00]{1,250}',b)]
report=dict(candidate=a.candidate,product=product,version=version,build=build,device_class=device,url=url,discovery=discovery,whole_ipsw_downloaded=False,proxy_used=False,ipsw_total_bytes=stream.size,build_manifest_sha256=hashlib.sha256((o/'BuildManifest.plist').read_bytes()).hexdigest(),components=components,kernelcache_sha256=hashlib.sha256(b).hexdigest(),kernelcache_bytes=len(b),cpu=hex(cpu),subtype=hex(subtype),darwin_versions=versions,apple_signature_verified=False,hardware_target_identity_inherited=False)
(o/'candidate.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
