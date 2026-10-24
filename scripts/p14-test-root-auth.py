from pathlib import Path
import ctypes as c,json,subprocess,uuid
r=Path(__file__).resolve().parents[1];o=r/'out/p14/host-tests';o.mkdir(exist_ok=True)
class View(c.Structure):
 _fields_=[('hashes',c.c_void_p),('image_bytes',c.c_uint64),('block_count',c.c_uint64),('generation',c.c_uint64),('volume_uuid',c.c_uint8*16),('image_sha256',c.c_uint8*32)]
lib=c.CDLL(str(r/'out/p12/verified-root-host/verified-root.dylib'));lib.vr_host_open.argtypes=[c.c_void_p,c.c_size_t,c.c_void_p,c.c_uint64,c.POINTER(View)];lib.vr_host_block.argtypes=[c.POINTER(View),c.c_uint64,c.c_void_p,c.c_size_t]
# Exercise the exact predicate included in the actual bsd_init object.
wrap=o/'decision.c';wrap.write_text('#include "'+str(r/'drivers/p14/RootAuth/RootDecision.h')+'"\nint decide(int ro,unsigned d,unsigned s,unsigned n,unsigned m,int v){return p14RootDecision(ro,d,s,n,m,v);}\n')
subprocess.run(['/usr/bin/clang','-dynamiclib','-O2',str(wrap),'-o',str(o/'decision.dylib')],check=True);gate=c.CDLL(str(o/'decision.dylib'));gate.decide.argtypes=[c.c_int,c.c_uint32,c.c_uint32,c.c_uint32,c.c_uint32,c.c_int]
base=(1,0x1000006,0x1000006,0xffffffff,1,1);rows=[]
for name,args,want in [('correct-root',base,1),('writable-root',(0,*base[1:]),0),('wrong-device',(1,0x1000007,*base[2:]),0),('missing-device',(1,0xffffffff,*base[2:]),0),('duplicate-media',(*base[:4],2,1),0),('missing-media',(*base[:4],0,1),0),('missing-verified-root',(*base[:5],0),0)]:
 got=gate.decide(*args);assert got==want;rows.append(dict(case=name,accepted=bool(got),scope='actual shared BSD decision predicate; no IOKit emulation'))
policies=[]
for name in ['OS','SystemOS']:
 p=r/'out/p12/policies'/name;raw=(p/'manifest.bin').read_bytes();key=(p/'trusted-key.bin').read_bytes();view=View();assert lib.vr_host_open(raw,len(raw),key,1,c.byref(view))==0
 actual=str(uuid.UUID(bytes=bytes(view.volume_uuid))).upper();assert actual==json.loads((p/'verification.json').read_text())['volume_uuid'];policies.append(dict(name=name,uuid=actual,signature_verified_by_actual_C=True))
 for case,changed in [('wrong-signature',raw[:160]+bytes([raw[160]^1])+raw[161:]),('wrong-uuid-without-resigning',raw[:40]+bytes([raw[40]^1])+raw[41:]),('wrong-block-table',raw[:-1]+bytes([raw[-1]^1]))]:
  v=View();rc=lib.vr_host_open(changed,len(changed),key,1,c.byref(v));assert rc!=0 and not v.hashes;rows.append(dict(case=name+'-'+case,vr_status=rc,accepted=False))
# Genuine signed different UUID: signature remains valid, identity binding must reject.
p=r/'out/p12/policies/OS';raw=(p/'manifest.bin').read_bytes();key=(p/'trusted-key.bin').read_bytes();header=bytearray(raw[:160]);header[40]^=1;(o/'wrong-uuid-header.bin').write_bytes(header)
subprocess.run(['/usr/bin/openssl','dgst','-sha256','-sign',str(r/'out/p12/policies/private/owner.pem'),'-out',str(o/'wrong-uuid-signature.bin'),str(o/'wrong-uuid-header.bin')],check=True)
wrong=bytes(header)+(o/'wrong-uuid-signature.bin').read_bytes()+raw[544:];v=View();assert lib.vr_host_open(wrong,len(wrong),key,1,c.byref(v))==0
wanted=uuid.UUID('04769D26-2CA0-4825-B443-E2E5F88300F2').bytes;assert bytes(v.volume_uuid)!=wanted;assert gate.decide(1,base[1],base[2],base[3],1,int(bytes(v.volume_uuid)==wanted))==0
rows.append(dict(case='genuinely-signed-wrong-uuid',signature_valid=True,identity_matches=False,accepted=False,scope='C signature verification plus identity decision; live provider chain untested'))
# Real OS block is accepted, one changed byte is rejected.
v=View();assert lib.vr_host_open(raw,len(raw),key,1,c.byref(v))==0
with (r/'out/p12/reference/cheeseburger-p12-reference/userdata/userdata-root.img').open('rb') as f:f.seek(1048576);block=f.read(4096)
assert lib.vr_host_block(c.byref(v),0,block,len(block))==0;damaged=bytes([block[0]^1])+block[1:];assert lib.vr_host_block(c.byref(v),0,damaged,len(damaged))!=0
rows.append(dict(case='actual-block-tamper',original_accepted=True,changed_rejected=True))
(o/'result.json').write_text(json.dumps(dict(result='HOST_ROOT_AUTH_GATES_PASS',cases=rows,policies=policies,real_iokit_registry_tested=False,hardware_tested=False),indent=2)+'\n');print('Host root-auth gates PASS:',len(rows),'cases; live registry and hardware not claimed')
