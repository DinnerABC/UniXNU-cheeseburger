#!/usr/bin/env python3
"""Export real AArch64 build products and check the three integrated FV payloads."""
import argparse,hashlib,json,shutil,uuid
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
s=a.source.resolve();o=a.output.resolve();o.mkdir(parents=True,exist_ok=True)
b=s/'workspace/Build/cheeseburger/RELEASE_GCC5';bins=b/'AARCH64';manifest={}
for f in bins.glob('*.efi'):
 data=f.read_bytes()
 if data[:2]!=b'MZ':continue
 off=int.from_bytes(data[60:64],'little')
 assert data[off:off+4]==b'PE\0\0'
 assert int.from_bytes(data[off+4:off+6],'little')==0xaa64,f.name
 manifest[f.name]={'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'machine':'AARCH64'}
 if f.name in ['DxeCore.efi','ArmPlatformPrePiUniCore.efi','UniXNUDxe.efi','DarwinBoot.efi']:
  (o/'efi').mkdir(exist_ok=True);shutil.copy2(f,o/'efi'/f.name)
assert 'DxeCore.efi' in manifest
fv=next(f for f in (b/'FV').iterdir() if f.name.lower()=='fvmain.fv')
raw=fv.read_bytes();assert raw[40:44]==b'_FVH'
entries={};pos=(int.from_bytes(raw[48:50],'little')+7)&~7
while pos+24<=len(raw) and raw[pos:pos+24]!=b'\xff'*24:
 size=int.from_bytes(raw[pos+20:pos+23],'little');header=24
 if size==0xffffff:size=int.from_bytes(raw[pos+24:pos+32],'little');header=32
 if size<header:break
 guid=str(uuid.UUID(bytes_le=raw[pos:pos+16])).upper();q=pos+header;end=pos+size
 while q+4<=end:
  z=int.from_bytes(raw[q:q+3],'little');typ=raw[q+3];h=4
  if z==0xffffff:z=int.from_bytes(raw[q+4:q+8],'little');h=8
  if z<h:break
  if typ==0x10:entries[guid]={'bytes':z-h,'sha256':hashlib.sha256(raw[q+h:q+z]).hexdigest()}
  q=(q+z+3)&~3
 pos=(pos+size+7)&~7
integrated=(s/'P2Integration').exists()
if integrated:
 for guid,name in [('9B665204-A133-47A2-9396-9E2B370A4E25','UniXNU'),('29B950CF-586D-49EA-9D71-F88645CCDAF6','UniXNUDxe'),('1B64AF87-8415-4B57-9B16-680D70913B9F','DarwinBoot')]:
  assert guid in entries,(guid,name)
  f=s/'P2Integration/UniXNU.efi' if name=='UniXNU' else bins/(name+'.efi')
  assert entries[guid]['sha256']==hashlib.sha256(f.read_bytes()).hexdigest(),name
shutil.copy2(s/'Platforms/Msm8998Pkg/Device/oneplus/cheeseburger/DeviceTreeBlob/Android/Android-cheeseburger.dtb',o/'Android-cheeseburger.dtb')
shutil.copy2(fv,o/'FVMAIN.Fv')
for f in (b/'FV').glob('*.map'):shutil.copy2(f,o/f.name)
(o/'compiled-efi-manifest.json').write_text(json.dumps({'built_pe_images':manifest,'fv_pe_sections':entries,'integration_verified':integrated,'hardware_validated':False},indent=2)+'\n')
print('AArch64 PE images:',len(manifest),'FV PE sections:',len(entries),'integration verified:',integrated)
