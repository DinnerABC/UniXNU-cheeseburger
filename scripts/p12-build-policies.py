from pathlib import Path
import hashlib,importlib.util,json,os,subprocess,ctypes,io
r=Path(__file__).resolve().parents[1];o=r/'out/p12/policies';o.mkdir(exist_ok=True);source=r/'.work/p4-unixnu/kits/VerifiedRoot/tools/manifest.py';spec=importlib.util.spec_from_file_location('p12_manifest',source);mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
report_path=r/'out/p12/reference/cheeseburger-p12-reference/userdata/userdata-root.json';report=json.loads(report_path.read_text());image=report_path.with_name('userdata-root.img');assert image.stat().st_size==report['image_bytes']
metadata=json.loads((r/'out/p12/reference/metadata.json').read_text());keys=o/'private';keys.mkdir(mode=0o700,exist_ok=True);os.chmod(keys,0o700);key=keys/'owner.pem';public=o/'owner-public.pem'
if not key.exists():
 fd=os.open(key,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600);os.close(fd);subprocess.run(['openssl','genpkey','-algorithm','RSA','-pkeyopt','rsa_keygen_bits:3072','-out',str(key)],capture_output=True,check=True)
os.chmod(key,0o600);subprocess.run(['openssl','pkey','-in',str(key),'-pubout','-out',str(public)],capture_output=True,check=True)
class Window(io.RawIOBase):
 def __init__(self,offset,size):self.file=image.open('rb',buffering=8<<20);self.file.seek(offset);self.left=size
 def read(self,n=-1):
  if n<0:n=self.left
  b=self.file.read(min(n,self.left));self.left-=len(b);return b
 def close(self):self.file.close();super().close()
class View(ctypes.Structure):
 _fields_=[('hashes',ctypes.c_void_p),('image_bytes',ctypes.c_uint64),('block_count',ctypes.c_uint64),('generation',ctypes.c_uint64),('volume_uuid',ctypes.c_uint8*16),('image_sha256',ctypes.c_uint8*32)]
lib=ctypes.CDLL(str(r/'out/p12/verified-root-host/verified-root.dylib'));lib.vr_host_open.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_void_p,ctypes.c_uint64,ctypes.POINTER(View)];lib.vr_host_block.argtypes=[ctypes.POINTER(View),ctypes.c_uint64,ctypes.c_void_p,ctypes.c_size_t]
policies=[];assembly=['.section __DATA_CONST,__const\n.balign 8\n']
for w in report['windows']:
 name=w['name'];m=metadata['OS' if name=='OS' else 'Cryptex1,SystemOS'];volumes=[e for e in m['entities'] if e.get('FilesystemType')=='apfs' and e.get('VolumeUUID')];assert len(volumes)==1;uuid=volumes[0]['VolumeUUID'];dest=o/name
 with Window(w['offset'],w['size']) as stream:record=mod.make(stream,w['size'],key,public,uuid,1,dest)
 assert record['image_sha256']==w['sha256'];envelope=(dest/'manifest.bin').read_bytes();anchor=(dest/'trusted-key.bin').read_bytes();view=View();assert lib.vr_host_open(envelope,len(envelope),anchor,1,ctypes.byref(view))==0
 h=hashlib.sha256();count=0
 with Window(w['offset'],w['size']) as stream:
  while b:=stream.read(4096):
   assert lib.vr_host_block(ctypes.byref(view),count,b,len(b))==0;h.update(b);count+=1
 assert count==view.block_count and h.hexdigest()==w['sha256']
 record.update(real_c_signature_and_all_blocks_verified=True,actual_blocks_verified=count,root_manifest_window_hash_match=True,source_volume=m['entities'][-1],apple_signature_verified=False,reference_only=True,install_allowed=False);(dest/'verification.json').write_text(json.dumps(record,indent=2)+'\n');policies.append(dict(name=name,manifest=str((dest/'manifest.bin').relative_to(r)),bytes=len(envelope),volume_uuid=uuid,image_sha256=h.hexdigest(),blocks=count))
 symbol='_verified_root_'+name.lower()+'_manifest';assembly+=['.balign 8\n.globl '+symbol+'\n'+symbol+':\n.incbin "'+str(dest/'manifest.bin')+'"\n'];print('FULLY VERIFIED',name,count,uuid,flush=True)
assembly+=['.balign 8\n.globl _verified_root_key\n_verified_root_key:\n.incbin "'+str(o/'OS/trusted-key.bin')+'"\n.balign 8\n.globl _verified_root_policies,_verified_root_policy_count\n_verified_root_policies:\n']
assert (o/'OS/trusted-key.bin').read_bytes()==(o/'SystemOS/trusted-key.bin').read_bytes()
for p in policies:assembly+=['.quad _verified_root_'+p['name'].lower()+'_manifest, '+str(p['bytes'])+'\n']
assembly+=['_verified_root_policy_count:\n.quad '+str(len(policies))+'\n'];(o/'inputs.S').write_text(''.join(assembly));subprocess.run(['xcrun','clang','-target','arm64-apple-ios17.0','-c',str(o/'inputs.S'),'-o',str(o/'inputs.o')],check=True)
subprocess.run(['xcrun','ar','rcs',str(o/'libVerifiedRootPolicy.a'),str(o/'inputs.o')],check=True)
(o/'result.json').write_text(json.dumps({'policies':policies,'signature_scope':'Owner-signed ordered block hashes, not Apple signature/seal verification','private_key':str(key.relative_to(r)),'reference_only':True,'install_allowed':False},indent=2)+'\n')
