from pathlib import Path
import hashlib,json,shutil
r=Path(__file__).resolve().parents[1];out=r/'out/p12';pack=json.loads((out/'uefi/packaging.json').read_text());gate=json.loads((out/'integration/result.json').read_text())
required=['build-ufs-02','build-verifier-01','archive-policies-01','build-userdata-01','build-policies-01','link-native-01','prelink-01','audit-registration-01','qualify-prelink-01','integration-01','loader-01','check-adt-01','backed-root-os-01','backed-root-systemos-01','system-consistency-02','root-integration-01','build-uefi-01','export-uefi-01','verify-uefi-01','pack-bootimg-02']
for label in required:assert json.loads((out/'logs'/(label+'.json')).read_text())['exit_code']==0,label
files=['native/kernel.macho','kernel/kernel.macho','kernel/prelink-report.json','adt/kernel.macho.adt','payload/payload.bundle','uefi/MSM8998PKG_UEFI.fd','uefi/boot-p12-reference.img','pmgr/libCheeseburgerPlatformPower.a','native-ufs/libNativeUfs.a','integration/result.json','qualification/result.json','root-integration.json','verifier/libVerifiedRoot.a','policies/libVerifiedRootPolicy.a','policies/OS/manifest.bin','policies/SystemOS/manifest.bin','policies/OS/trusted-key.bin','system-consistency-v2/result.json','source-pins.json','geometry.json','driver/source-provenance.json']
items={}
for rel in files:
 p=out/rel;data=p.read_bytes();items[rel]=dict(bytes=len(data),sha256=hashlib.sha256(data).hexdigest())
assert items['uefi/boot-p12-reference.img']['sha256']==pack['sha256']
assert items['kernel/kernel.macho']['sha256']==gate['kernel_sha256']
sources={}
for parent in [r/'drivers/CheeseburgerPlatformPower',r/'drivers/p12',r/'patches/p12',r/'config/p12',r/'tools/p12']:
 for p in parent.rglob('*'):
  if p.is_file():sources[str(p.relative_to(r))]=hashlib.sha256(p.read_bytes()).hexdigest()
for p in (r/'scripts').glob('p12*'):
 if p.is_file():sources[str(p.relative_to(r))]=hashlib.sha256(p.read_bytes()).hexdigest()
root=json.loads((out/'root-integration.json').read_text());assert root['result']=='OFFLINE_ROOT_INTEGRATION_PASS'
ref=json.loads((out/'reference/cheeseburger-p12-reference/userdata/userdata-root.json').read_text());items['reference/cheeseburger-p12-reference/userdata/userdata-root.img']={'bytes':ref['image_bytes'],'sha256':ref['image_sha256'],'verification':'Full file SHA256 rechecked by root-integration-01, plus every window block verified by actual C verifier'}
if (r/'out/p10/source-revisions.json').exists():shutil.copy2(r/'out/p10/source-revisions.json',out/'source-revisions.json')
documents={str(p.relative_to(r)):hashlib.sha256(p.read_bytes()).hexdigest() for p in (r/'docs').glob('P12_*.md')}
report=dict(document_sha256=documents,result='P12_REFERENCE_OFFLINE_ONLY_NOT_INSTALLABLE',files=items,source_sha256=sources,original_security_code_preserved=True,pmgr_compatibility_only=True,qualcomm_hardware_power_backend=False,native_ufs_read_only=True,runtime_verified=False,safe_to_flash=False,install_allowed=False,reference_only=True,iphone_target_unchanged=True,apple_boot_chain_verified=False,no_phone_commands_in_p12=True,successful_logged_stages=required)
(out/'manifest.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'image':pack['image'],'sha256':pack['sha256'],'kernel_sha256':gate['kernel_sha256']}))
