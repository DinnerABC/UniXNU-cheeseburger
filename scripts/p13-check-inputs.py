#!/usr/bin/env python3
"""Read-only P12 acceptance recheck for P13; no changes to the P12 baseline."""
from pathlib import Path
import ctypes,hashlib,json,struct,sys,tomllib,uuid
r=Path(__file__).resolve().parents[1];o=r/'out/p13';m=json.loads((r/'out/p12/manifest.json').read_text());files={}
def sha(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  while b:=f.read(8<<20):h.update(b)
 return h.hexdigest()
for name,pin in m['files'].items():
 p=r/'out/p12'/name;assert p.stat().st_size==pin['bytes'] and sha(p)==pin['sha256'],name;files[name]=pin
for name,h in m['source_sha256'].items():assert sha(r/name)==h,name
for name,h in m['document_sha256'].items():assert sha(r/name)==h,name
rootname='reference/cheeseburger-p12-reference/userdata/userdata-root.img';image=r/'out/p12'/rootname;report=json.loads(image.with_suffix('.json').read_text());policies=json.loads((r/'out/p12/policies/result.json').read_text())['policies'];meta=json.loads((r/'out/p12/reference/metadata.json').read_text());systems=json.loads((r/'out/p12/system-consistency-v2/result.json').read_text());adt=json.loads((r/'out/p12/adt/validation.json').read_text());board=tomllib.loads((r/'config/p12/board.toml').read_text())
assert board['ipsw']['build']=='21H461' and board['ipsw']['product']=='iPad7,1';assert systems['OS']['system_version']['ProductBuildVersion']==systems['SystemOS']['system_version']['ProductBuildVersion']=='21H461'
with image.open('rb') as f:
 header=f.read(4096);assert header[:8]==b'SHNGROOT' and hashlib.sha256(header).hexdigest()==report['manifest_sha256']
class View(ctypes.Structure):
 _fields_=[('hashes',ctypes.c_void_p),('image_bytes',ctypes.c_uint64),('block_count',ctypes.c_uint64),('generation',ctypes.c_uint64),('volume_uuid',ctypes.c_uint8*16),('image_sha256',ctypes.c_uint8*32)]
lib=ctypes.CDLL(str(r/'out/p12/verified-root-host/verified-root.dylib'));lib.vr_host_open.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_void_p,ctypes.c_uint64,ctypes.POINTER(View)];checked=[]
for policy in policies:
 name=policy['name'];blob=(r/policy['manifest']).read_bytes();key=(r/'out/p12/policies'/name/'trusted-key.bin').read_bytes();view=View();envelope=ctypes.create_string_buffer(blob);anchor=ctypes.create_string_buffer(key)
 assert lib.vr_host_open(envelope,len(blob),anchor,1,ctypes.byref(view))==0
 assert str(uuid.UUID(bytes=bytes(view.volume_uuid))).upper()==policy['volume_uuid']==systems[name]['volume_uuid']
 w=next(x for x in report['windows'] if x['name']==name);assert view.image_bytes==w['size'] and bytes(view.image_sha256).hex()==policy['image_sha256']==w['sha256'];checked.append({'name':name,'signature_verified':True,'uuid':policy['volume_uuid'],'image_sha256':policy['image_sha256'],'bytes':view.image_bytes})
assert adt['root_volume_uuid']==checked[0]['uuid']==board['payload']['adt']['root_match_volume_uuid']
sys.path.insert(0,str(r/'tools/ram_payload'));import bundle;bundle.MAX_BYTES=256*1024*1024
pack=json.loads((r/'out/p12/uefi/packaging.json').read_text());boot=r/pack['image'];b=boot.read_bytes();ks,ka,rs,ra,ss,sa,ta,page,ver,_=struct.unpack_from('<10I',b,8);assert (ka,ra,ta,page,ver,ss)==(0x8000,0x1000000,0x100,4096,0,0);off=page+(ks+page-1)//page*page;payload=bundle.verify(b[off:off+rs]);assert hashlib.sha256(payload['\\kernel.macho']).hexdigest()==m['files']['kernel/kernel.macho']['sha256'];assert payload['\\kernel.macho.adt']==(r/'out/p12/adt/kernel.macho.adt').read_bytes()
seed=[p for p in payload if any(s in p.lower() for s in ['seed','ramdisk','data.dbr','data.img'])];assert not seed
# 64-bit raw file and Android sparse block counts fit the installed host format.
blocks=(image.stat().st_size+4095)//4096;assert blocks<2**32 and image.stat().st_size>2**32
result={'result':'P13_P12_INPUTS_REVALIDATED','image':str(image.relative_to(r)),'image_bytes':image.stat().st_size,'image_sha256':files[rootname]['sha256'],'boot_image':str(boot.relative_to(r)),'boot_bytes':len(b),'boot_sha256':pack['sha256'],'root_uuid':adt['root_volume_uuid'],'policies':checked,'p12_frozen_files_verified':len(files),'data_seed_present':False,'root_mount_experiment_only':True,'launchd_and_writable_data_ready':False,'apple_rootauth_enforcement':'Original VNOP_IOCTL FSIOC_KERNEL_ROOTAUTH remains enforced; no bypass','sparse_block_bytes':4096,'sparse_total_blocks':blocks,'user_authorization':'P13 one-time reference experiment; final iPhone target unchanged','partition_written':False,'hardware_root_mount_proven':False}
(o/'inputs.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
