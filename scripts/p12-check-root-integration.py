#!/usr/bin/env python3
"""Check actual root data, linked signed policies, frozen evidence and backup files."""
from pathlib import Path
import hashlib,json,struct,sys,tomllib
r=Path(__file__).resolve().parents[1];o=r/'out/p12';sys.path.insert(0,str(r/'.work/p4-unixnu/kits/NativePrelink'));import native_prelink as np
def load(p):return json.loads(p.read_text())
def sha(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  while b:=f.read(8<<20):h.update(b)
 return h.hexdigest()
userdata=o/'reference/cheeseburger-p12-reference/userdata/userdata-root.img';report=load(userdata.with_suffix('.json'));geometry=load(o/'geometry.json')['userdata'];assert userdata.stat().st_size==report['image_bytes']<=geometry['capacity_bytes']
assert sha(userdata)==report['image_sha256']
with userdata.open('rb') as f:assert hashlib.sha256(f.read(4096)).hexdigest()==report['manifest_sha256']
policies=load(o/'policies/result.json')['policies'];adt=load(o/'adt/validation.json');board=tomllib.loads((r/'config/p12/board.toml').read_text());assert adt['root_volume_uuid']==board['payload']['adt']['root_match_volume_uuid']==policies[0]['volume_uuid']
b=(o/'kernel/kernel.macho').read_bytes();m=np.macho(b);symbols={n:a for a,ns in np.exports(b,m).items() for n in ns}
def read(va,n):
 at=np.backed_offset(m['segments'],va,n,len(b));return b[at:at+n]
assert struct.unpack('<Q',read(symbols['_verified_root_policy_count'],8))[0]==2
ptr=symbols['_verified_root_policies'];embedded=[]
for i,policy in enumerate(policies):
 a,n=struct.unpack('<2Q',read(ptr+16*i,16));data=(r/policy['manifest']).read_bytes();assert n==len(data) and read(a,n)==data
 window=next(w for w in report['windows'] if w['name']==policy['name']);assert window['sha256']==policy['image_sha256']
 embedded.append({'name':policy['name'],'bytes':n,'va':hex(a),'sha256':hashlib.sha256(data).hexdigest(),'uuid':policy['volume_uuid']})
key=(o/'policies/OS/trusted-key.bin').read_bytes();assert read(symbols['_verified_root_key'],len(key))==key
assert '_vr_xnu_crypto' in symbols and '__ZN14NativeUfsBlock10gMetaClassE' in symbols
span=max(s['va']+s['size'] for s in m['segments'] if s['size'])-min(s['va'] for s in m['segments'] if s['size']);assert span<512*1024*1024
payload=load(o/'payload/manifest.json');assert payload['bytes']<=256*1024*1024
pins=load(o/'p11-pins.json');frozen=[]
for name,expected in pins.items():
 p=r/'out/p11'/name;assert p.stat().st_size==expected['bytes'] and sha(p)==expected['sha256'];frozen.append(name)
# Check only local backup integrity. This is not a phone restore rehearsal.
required={'persist','modemst1','modemst2','fsg','fsc','boot','recovery','abl','xbl','tz','rpm','hyp','devcfg'};backups=[]
for p in (r/'backups/p3').glob('*/manifest.json'):
 rows=load(p)
 if not isinstance(rows,list):continue
 for row in rows:
  if row.get('partition') not in required:continue
  f=r/row['file'];assert row['verified'] and row['exit_code']==0
  assert f.stat().st_size==row['expected_bytes']==row['actual_bytes'] and sha(f)==row['sha256']
  backups.append(row['partition'])
assert required<=set(backups)
source=r/'drivers/p12/NativeUfs/NativeUfs.cpp';provenance=load(o/'driver/source-provenance.json');provenance['adapter_sha256']=sha(source);provenance['final_root_match_guard']='Parsed chosen/root-matching UUID must equal signed OS policy';(o/'driver/source-provenance.json').write_text(json.dumps(provenance,indent=2)+'\n')
result=dict(result='OFFLINE_ROOT_INTEGRATION_PASS',userdata_sha256=report['image_sha256'],userdata_bytes=report['image_bytes'],userdata_capacity_bytes=geometry['capacity_bytes'],root_uuid=adt['root_volume_uuid'],embedded_policies=embedded,linked_crypto_provider=True,native_ufs_block_linked=True,kernel_virtual_span=span,loader_owned_window_bytes=512*1024*1024,payload_bytes=payload['bytes'],payload_max_bytes=256*1024*1024,p11_artifacts_unchanged=frozen,local_critical_backup_integrity=sorted(set(backups)),backup_restore_rehearsed=False,live_manifest_status=None,target_iomedia_proven=False,target_apfs_mount_proven=False,reference_only=True,install_allowed=False,apple_boot_chain_verified=False)
(o/'root-integration.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
