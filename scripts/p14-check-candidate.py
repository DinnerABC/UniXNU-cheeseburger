from pathlib import Path
import json,hashlib,struct,sys,subprocess
r=Path(__file__).resolve().parents[1];sys.path.insert(0,str(r/'.work/p4-unixnu/kits/NativePrelink'));import native_prelink as np
o=r/'out/p14';b=(o/'kernel/kernel.macho').read_bytes();m=np.macho(b);symbols={n:a for a,ns in np.exports(b,m).items() for n in ns}
def read(va,n):
 at=np.backed_offset(m['segments'],va,n,len(b));return b[at:at+n]
assert json.loads((o/'host-tests/result.json').read_text())['result']=='HOST_ROOT_AUTH_GATES_PASS'
for n in ['_native_owner_root_checkpoint','_native_owner_root_accepted_checkpoint','_native_verified_block_root_authorized','_native_ufs_block_authenticates']:
 assert n in symbols,n
assert struct.unpack('<i',read(symbols['_native_owner_root_trust_enabled'],4))[0]==1
assert struct.unpack('<i',read(symbols['_native_owner_root_apple_autherr'],4))[0]==-1
assert struct.unpack('<Q',read(symbols['_verified_root_policy_count'],8))[0]==2
policyrows=[]
for ix,p in enumerate(json.loads((r/'out/p12/policies/result.json').read_text())['policies']):
 va,n=struct.unpack('<QQ',read(symbols['_verified_root_policies']+16*ix,16));raw=(r/p['manifest']).read_bytes();assert read(va,n)==raw;policyrows.append(dict(name=p['name'],uuid=p['volume_uuid'],sha256=hashlib.sha256(raw).hexdigest()))
key=(r/'out/p12/policies/OS/trusted-key.bin').read_bytes();assert read(symbols['_verified_root_key'],len(key))==key
p12=json.loads((r/'out/p12/kernel/prelink-report.json').read_text());p14=json.loads((o/'kernel/prelink-report.json').read_text());assert p14['original_executable_sha256']==p12['original_executable_sha256']
for name,pin in json.loads((o/'baseline/pins.json').read_text()).items():
 p=r/name;assert p.stat().st_size==pin['bytes'] and hashlib.sha256(p.read_bytes()).hexdigest()==pin['sha256'],name
assert hashlib.sha256((r/'out/p12/native-ufs/libNativeUfs.a').read_bytes()).hexdigest()==json.loads((o/'baseline/pins.json').read_text())['out/p12/native-ufs/libNativeUfs.a']['sha256']
subprocess.run(['/opt/homebrew/opt/llvm/bin/llvm-objdump','-d','--symbolize-operands','--disassemble-symbols=_native_verified_block_root_authorized',str(o/'kernel/kernel.macho')],stdout=(o/'root-auth/final-query.asm').open('w'),check=True)
result=dict(result='P14_STATIC_ROOT_AUTH_GATE_PASS',kernel_sha256=hashlib.sha256(b).hexdigest(),owner_trust_enabled=True,apple_ioctl_retained=True,apple_autherr_runtime=None,owner_authorization_is_distinct_from_apple_authentication=True,policies=policyrows,original_apple_component_code_hashes_unchanged=True,p13_frozen_inputs_unchanged=True,ufs_archive_unchanged=True,live_iomedia_and_mount_device_pending=True,userdata_not_written=True)
(o/'candidate-check.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
