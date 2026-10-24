#!/usr/bin/env python3
"""Read Apple ZIP metadata with bounded ranges, never the whole IPSW."""
from pathlib import Path
import sys,json,zipfile,plistlib,hashlib
r=Path(__file__).resolve().parents[1];sys.path.insert(0,str(r.parent/'upstream/UniXNU/scripts'))
from unixnu.ipsw import RangeFile
p=r/'out/p8/ios';candidate=json.loads((p/'iphone-candidate.json').read_text());f=candidate['firmware'];url=f['url'];assert url.startswith('https://updates.cdn-apple.com/')
stream=RangeFile(url,block=256*1024,retries=2);assert stream.size==f['filesize'];z=zipfile.ZipFile(stream)
b=z.read('BuildManifest.plist');(p/'BuildManifest.plist').write_bytes(b);v=plistlib.loads(b);assert (v['ProductVersion'],v['ProductBuildVersion'])==('17.6','21G80') and 'iPhone11,8' in v['SupportedProductTypes']
identities=[]
for i,identity in enumerate(v['BuildIdentities']):
 if identity['Info'].get('RestoreBehavior')!='Erase':continue
 components={}
 for name in ['KernelCache','DeviceTree','StaticTrustCache','OS','SystemOS','Cryptex1,SystemOS','Cryptex1,SystemTrustCache','Cryptex1,SystemVolume','SystemVolume','Ap,SystemVolumeCanonicalMetadata']:
  item=identity['Manifest'].get(name)
  if not item:continue
  path=item.get('Info',{}).get('Path');info=z.getinfo(path) if path else None
  components[name]=dict(path=path,digest=item.get('Digest',b'').hex(),zip_bytes=info.file_size if info else None,compressed_bytes=info.compress_size if info else None)
 identities.append(dict(index=i,device_class=identity['Info']['DeviceClass'],chip_id=identity['ApChipID'],board_id=identity['ApBoardID'],components=components))
report=dict(source=url,bytes_total=stream.size,manifest_sha256=hashlib.sha256(b).hexdigest(),product_version=v['ProductVersion'],build=v['ProductBuildVersion'],products=v['SupportedProductTypes'],identities=identities,whole_ipsw_downloaded=False,authenticity_signature_verified=False,runtime_compatible=False)
(p/'apple-manifest.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
