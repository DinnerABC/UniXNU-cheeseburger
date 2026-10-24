from pathlib import Path
import hashlib,json,shutil
r=Path(__file__).resolve().parents[1];out=r/'out/p11';pack=json.loads((out/'uefi/packaging.json').read_text());gate=json.loads((out/'integration/result.json').read_text())
files=['native/kernel.macho','kernel/kernel.macho','kernel/prelink-report.json','adt/kernel.macho.adt','payload/payload.bundle','uefi/MSM8998PKG_UEFI.fd','uefi/boot-p11-xnu.img','pmgr/libCheeseburgerPlatformPower.a','native-ufs/libNativeUfs.a','integration/result.json','qualification/result.json']
items={}
for rel in files:
 p=out/rel;data=p.read_bytes();items[rel]=dict(bytes=len(data),sha256=hashlib.sha256(data).hexdigest())
assert items['uefi/boot-p11-xnu.img']['sha256']==pack['sha256']
assert items['kernel/kernel.macho']['sha256']==gate['kernel_sha256']
sources={}
for parent in [r/'drivers/CheeseburgerPlatformPower',r/'drivers/p11',r/'patches/p11']:
 for p in parent.rglob('*'):
  if p.is_file():sources[str(p.relative_to(r))]=hashlib.sha256(p.read_bytes()).hexdigest()
for p in (r/'scripts').glob('p11*'):
 if p.is_file():sources[str(p.relative_to(r))]=hashlib.sha256(p.read_bytes()).hexdigest()
if (r/'out/p10/source-revisions.json').exists():shutil.copy2(r/'out/p10/source-revisions.json',out/'source-revisions.json')
report=dict(result='P11_INTEGRATED_CANDIDATE_OFFLINE_PASS',files=items,source_sha256=sources,original_security_code_preserved=True,pmgr_compatibility_only=True,qualcomm_hardware_power_backend=False,native_ufs_read_only=True,runtime_verified=False,safe_to_flash=False)
(out/'manifest.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'image':pack['image'],'sha256':pack['sha256'],'kernel_sha256':gate['kernel_sha256']}))
